// This file contains the common code every implementation uses for stage2
// It is intended to be included multiple times and compiled multiple times
// We assume the file in which it is include already includes
// "simdjson/stage2.h" (this simplifies amalgation)

#include "generic/stage2/tape_writer.h"
#include "generic/stage2/logger.h"
#include "generic/stage2/atomparsing.h"
#include "generic/stream.h"

namespace simdjson {
namespace SIMDJSON_IMPLEMENTATION {
namespace stage2 {

namespace {

struct structural_parser : stream::json {
  /** Lets you append to the tape */
  tape_writer tape;
  dom_parser_implementation &parser;

  // For non-streaming, to pass an explicit 0 as next_structural, which enables optimizations
  really_inline structural_parser(dom_parser_implementation &_parser, uint32_t start_structural_index)
    : stream::json(&_parser.structural_indexes[start_structural_index], _parser.buf, _parser.doc->string_buf.get()),
      tape{_parser.doc->tape.get()},
      parser{_parser} {
  }

  really_inline void start_scope(bool in_object) {
    parser.containing_scope[depth].tape_index = next_tape_index();
    parser.containing_scope[depth].count = 0;
    tape.skip(); // We don't actually *write* the start element until the end.
    parser.in_object[depth] = in_object;
  }

  really_inline bool in_object() {
    return parser.in_object[depth];
  }

  WARN_UNUSED really_inline bool exceeded_max_depth() {
    bool exceeded = depth >= parser.max_depth();
    if (exceeded) { log_error("Exceeded max depth!"); }
    return exceeded;
  }

  WARN_UNUSED really_inline bool start_document() {
    log_start_value("document");
    start_scope(false);
    depth++;
    assert(depth == 1);
    return exceeded_max_depth();
  }

  // this function is responsible for annotating the start of the scope
  really_inline void end_scope(internal::tape_type start, internal::tape_type end) noexcept {
    // write our doc->tape location to the header scope
    // The root scope gets written *at* the previous location.
    tape.append(parser.containing_scope[depth].tape_index, end);
    // count can overflow if it exceeds 24 bits... so we saturate
    // the convention being that a cnt of 0xffffff or more is undetermined in value (>=  0xffffff).
    const uint32_t start_tape_index = parser.containing_scope[depth].tape_index;
    const uint32_t count = parser.containing_scope[depth].count;
    const uint32_t cntsat = count > 0xFFFFFF ? 0xFFFFFF : count;
    // This is a load and an OR. It would be possible to just write once at doc->tape[d.tape_index]
    tape_writer::write(parser.doc->tape[start_tape_index], next_tape_index() | (uint64_t(cntsat) << 32), start);
  }

  really_inline uint32_t next_tape_index() {
    return uint32_t(tape.next_tape_loc - parser.doc->tape.get());
  }

  really_inline void end_document() {
    depth--;
    log_end_value("document");
    end_scope(internal::tape_type::ROOT, internal::tape_type::ROOT);
  }

  // increment_count increments the count of keys in an object or values in an array.
  // Note that if you are at the level of the values or elements, the count
  // must be increment in the preceding depth (depth-1) where the array or
  // the object resides.
  really_inline void increment_count() {
    parser.containing_scope[depth - 1].count++; // we have a key value pair in the object at parser.depth - 1
  }

  really_inline uint8_t *on_start_string() noexcept {
    // we advance the point, accounting for the fact that we have a NULL termination
    tape.append(string_buf - parser.doc->string_buf.get(), internal::tape_type::STRING);
    return string_buf + sizeof(uint32_t);
  }

  really_inline void on_end_string(uint8_t *dst) noexcept {
    uint32_t str_length = uint32_t(dst - (string_buf + sizeof(uint32_t)));
    // TODO check for overflow in case someone has a crazy string (>=4GB?)
    // But only add the overflow check when the document itself exceeds 4GB
    // Currently unneeded because we refuse to parse docs larger or equal to 4GB.
    memcpy(string_buf, &str_length, sizeof(uint32_t));
    // NULL termination is still handy if you expect all your strings to
    // be NULL terminated? It comes at a small cost
    *dst = 0;
    string_buf = dst + 1;
  }

  WARN_UNUSED really_inline bool parse_string(const uint8_t *src, bool key = false) {
    log_value(key ? "key" : "string");
    uint8_t *dst = on_start_string();
    dst = stringparsing::parse_string(src, dst);
    if (dst == nullptr) {
      log_error("Invalid escape in string");
      return true;
    }
    on_end_string(dst);
    return false;
  }

  WARN_UNUSED really_inline bool parse_string(stream::raw_json_string str, bool key = false) {
    log_value(key ? "key" : "string");
    string_buf += sizeof(uint32_t);
    std::string_view s;
    if (str.unescape(string_buf).get(s)) {
      log_error("Invalid escape in string");
      return false;
    }
    // TODO check for overflow in case someone has a crazy string (>=4GB?)
    // But only add the overflow check when the document itself exceeds 4GB
    // Currently unneeded because we refuse to parse docs larger or equal to 4GB.
    uint32_t str_length = static_cast<uint32_t>(s.length());
    memcpy(string_buf-sizeof(uint32_t), &str_length, sizeof(uint32_t));
    // NULL termination is still handy if you expect all your strings to
    // be NULL terminated? It comes at a small cost
    *(string_buf++) = 0;
    return false;
  }

  WARN_UNUSED really_inline bool parse_number(const uint8_t *src) {
    log_value("number");
    bool succeeded = numberparsing::parse_number(src, tape);
    if (!succeeded) { log_error("Invalid number"); }
    return !succeeded;
  }

  really_inline bool parse_number_with_space_terminated_copy() {
    /**
    * We need to make a copy to make sure that the string is space terminated.
    * This is not about padding the input, which should already padded up
    * to len + SIMDJSON_PADDING. However, we have no control at this stage
    * on how the padding was done. What if the input string was padded with nulls?
    * It is quite common for an input string to have an extra null character (C string).
    * We do not want to allow 9\0 (where \0 is the null character) inside a JSON
    * document, but the string "9\0" by itself is fine. So we make a copy and
    * pad the input with spaces when we know that there is just one input element.
    * This copy is relatively expensive, but it will almost never be called in
    * practice unless you are in the strange scenario where you have many JSON
    * documents made of single atoms.
    */
    uint8_t *copy = static_cast<uint8_t *>(malloc(parser.len + SIMDJSON_PADDING));
    if (copy == nullptr) {
      return true;
    }
    memcpy(copy, buf, parser.len);
    memset(copy + parser.len, ' ', SIMDJSON_PADDING);
    size_t idx = peek_index(-1);
    bool result = parse_number(&copy[idx]); // parse_number does not throw
    free(copy);
    return result;
  }

  WARN_UNUSED really_inline bool parse_primitive_value_at_end(const uint8_t *src) {
    switch (*src) {
    case '"':
      return parse_string(src+1);
    case 't':
      log_value("true");
      if (!atomparsing::is_valid_true_atom(src, remaining_len())) { return true; }
      tape.append(0, internal::tape_type::TRUE_VALUE);
      return false;
    case 'f':
      log_value("false");
      if (!atomparsing::is_valid_false_atom(src, remaining_len())) { return true; }
      tape.append(0, internal::tape_type::FALSE_VALUE);
      return false;
    case 'n':
      log_value("null");
      if (!atomparsing::is_valid_null_atom(src, remaining_len())) { return true; }
      tape.append(0, internal::tape_type::NULL_VALUE);
      return false;
    case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      // Next line used to be an interesting functional programming exercise with
      // a lambda that gets passed to another function via a closure. This would confuse the
      // clangcl compiler under Visual Studio 2019 (recent release).
      return parse_number_with_space_terminated_copy();
    default:
      log_error("Document starts with a non-value character");
      return true;
    }
  }

  WARN_UNUSED really_inline bool parse_primitive_value(const uint8_t *src) {
    switch (*src) {
    case '"':
      return parse_string(src+1);
    case 't':
      log_value("true");
      if (!atomparsing::is_valid_true_atom(src)) { return true; }
      tape.append(0, internal::tape_type::TRUE_VALUE);
      return false;
    case 'f':
      log_value("false");
      if (!atomparsing::is_valid_false_atom(src)) { return true; }
      tape.append(0, internal::tape_type::FALSE_VALUE);
      return false;
    case 'n':
      log_value("null");
      if (!atomparsing::is_valid_null_atom(src)) { return true; }
      tape.append(0, internal::tape_type::NULL_VALUE);
      return false;
    case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return parse_number(src);
    default:
      log_error("Non-value found when value was expected!");
      return true;
    }
  }

  WARN_UNUSED really_inline error_code finish() {
    end_document();
    parser.next_structural_index = uint32_t(index - &parser.structural_indexes[0]);

    if (depth != 0) {
      log_error("Unclosed objects or arrays!");
      return parser.error = TAPE_ERROR;
    }

    return SUCCESS;
  }

  WARN_UNUSED really_inline error_code error() {
    /* We do not need the next line because this is done by parser.init_stage2(),
    * pessimistically.
    * parser.is_valid  = false;
    * At this point in the code, we have all the time in the world.
    * Note that we know exactly where we are in the document so we could,
    * without any overhead on the processing code, report a specific
    * location.
    * We could even trigger special code paths to assess what happened
    * carefully,
    * all without any added cost. */
    if (depth >= parser.max_depth()) {
      return parser.error = DEPTH_ERROR;
    }
    switch (*peek(-1)) {
    case '"':
      return parser.error = STRING_ERROR;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
      return parser.error = NUMBER_ERROR;
    case 't':
      return parser.error = T_ATOM_ERROR;
    case 'n':
      return parser.error = N_ATOM_ERROR;
    case 'f':
      return parser.error = F_ATOM_ERROR;
    default:
      return parser.error = TAPE_ERROR;
    }
  }

  really_inline void init() {
    logger::log_start();
    parser.error = UNINITIALIZED;
  }

  WARN_UNUSED really_inline error_code start() {
    // If there are no structurals left, return EMPTY
    if (at_end()) {
      return parser.error = EMPTY;
    }

    init();
    // Push the root scope (there is always at least one scope)
    if (start_document()) {
      return parser.error = DEPTH_ERROR;
    }
    return SUCCESS;
  }

  really_inline size_t remaining_len() {
    return parser.len - peek_index(-1);
  }

  really_inline bool past_end() {
    return index >= &parser.structural_indexes[parser.n_structural_indexes];
  }
  really_inline bool at_end() {
    return index == &parser.structural_indexes[parser.n_structural_indexes];
  }
  really_inline bool at_beginning() {
    return index == parser.structural_indexes.get();
  }
}; // struct structural_parser

template<bool STREAMING>
WARN_UNUSED static error_code parse_structurals(dom_parser_implementation &dom_parser, dom::document &doc) noexcept {
  dom_parser.doc = &doc;
  stage2::structural_parser parser(dom_parser, STREAMING ? dom_parser.next_structural_index : 0);
  error_code result = parser.start();
  if (result) { return result; }

  //
  // Read first value
  //
  {
    const uint8_t *src = parser.advance();
    switch (*src) {
    case '{':
      parser.start_scope(false);
      goto object_begin;
    case '[':
      parser.start_scope(false);
      // Make sure the outer array is closed before continuing; otherwise, there are ways we could get
      // into memory corruption. See https://github.com/simdjson/simdjson/issues/906
      if (!STREAMING) {
        if (parser.buf[dom_parser.structural_indexes[dom_parser.n_structural_indexes - 1]] != ']') {
          goto error;
        }
      }
      goto array_begin;
    default:
      if (parser.parse_primitive_value_at_end(src)) { goto error; }
      goto finish;
    }
  }

//
// { }
// { "key" :
//   ^^^^^^^
//
object_begin: {
  // Start the object
  const uint8_t *key_string;
  if (auto error = parser.first_object_field().get(key_string)) { return error; } // Read "key" : 
  if (parser.exceeded_max_depth()) { return DEPTH_ERROR; }
  if (!key_string) { goto object_end; }
  if (parser.parse_string(key_string, true)) { return STRING_ERROR; }
  goto object_value;
}

//
// "key" : <value> }
// "key" : <value> , "key" : <value>
//                 ^^^^^^^^^
//
object_continue: {
  const uint8_t *key_string;
  if (auto error = parser.next_object_field().get(key_string)) { return error; }
  if (!key_string) { goto object_end; }
  if (parser.parse_string(key_string, true)) { return STRING_ERROR; }
}

//
// "key" : <value>
//         ^^^^^^^
//
object_value: {
  parser.increment_count();
  const uint8_t *src = parser.advance();
  switch (*src) {
  case '{':
    parser.start_scope(true);
    goto object_begin;
  case '[':
    parser.start_scope(true);
    goto array_begin;
  default:
    if (parser.parse_primitive_value(src)) { goto error; }
    goto object_continue;
  }
}

//
// }
//  ^
//
object_end: {
  parser.end_scope(internal::tape_type::START_OBJECT, internal::tape_type::END_OBJECT);
  if (parser.in_object()) { goto object_continue; }
  if (parser.depth == 1) { goto finish; }
  goto array_continue;
}

//
// ]
//  ^
//
array_end: {
  parser.end_scope(internal::tape_type::START_ARRAY, internal::tape_type::END_ARRAY);
  if (parser.in_object()) { goto object_continue; }
  if (parser.depth == 1) { goto finish; }
  goto array_continue;
}

//
// [ ]
// [   <value>
//   ^
//
array_begin: {
  // Start the array
  if (!parser.first_array_element()) { goto array_end; }
  if (parser.exceeded_max_depth()) { return DEPTH_ERROR; }
  goto array_value;
}

//
// <value> , <value>
//         ^
//
array_continue: {
  bool has_element;
  if (auto error = parser.next_array_element().get(has_element)) { return error; }
  if (!has_element) { goto array_end; }
}

//
// [ <value>
// , <value>
//   ^^^^^^^
//
array_value: {
  parser.increment_count();
  const uint8_t *src = parser.advance();
  switch (*src) {
  case '{':
    parser.start_scope(false);
    goto object_begin;
  case '[':
    parser.start_scope(false);
    goto array_begin;
  default:
    if (parser.parse_primitive_value(src)) { goto error; }
    goto array_continue;
  }
}

finish: {
  return parser.finish();
}

error: {
  return parser.error();
}
}

} // namespace {}
} // namespace stage2

/************
 * The JSON is parsed to a tape, see the accompanying tape.md file
 * for documentation.
 ***********/
WARN_UNUSED error_code dom_parser_implementation::stage2(dom::document &_doc) noexcept {
  error_code result = stage2::parse_structurals<false>(*this, _doc);
  if (result) { return result; }

  // If we didn't make it to the end, it's an error
  if ( next_structural_index != n_structural_indexes ) {
    logger::log_string("More than one JSON value at the root of the document, or extra characters at the end of the JSON!");
    return error = TAPE_ERROR;
  }

  return SUCCESS;
}

/************
 * The JSON is parsed to a tape, see the accompanying tape.md file
 * for documentation.
 ***********/
WARN_UNUSED error_code dom_parser_implementation::stage2_next(dom::document &_doc) noexcept {
  return stage2::parse_structurals<true>(*this, _doc);
}

} // namespace SIMDJSON_IMPLEMENTATION
} // namespace simdjson
