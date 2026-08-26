/*
 * File:        naplps_interpreter.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Run a NAPLPS presentation record into a resolved display list
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/nabts_sink/naplps_interpreter.h) at tag v2.7.2
 * (commit fef0115a). Algorithmic bodies are intact; orc:: -> tbc::vbi::
 * and the vbi-services/nabts_page.h include replaced by nabts_page.h.
 */

#ifndef TBC_VBI_NAPLPS_INTERPRETER_H
#define TBC_VBI_NAPLPS_INTERPRETER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nabts_page.h"
#include "naplps_code_env.h"
#include "naplps_pdi.h"
#include "naplps_state.h"

namespace tbc::vbi {

/**
 * @brief Runs a NAPLPS presentation record and emits what it drew
 *
 * One instance per *service*, not per record: macros, DRCS characters, the
 * programmable texture masks and the colour map persist across records unless
 * a RESET clears them. CEA-516 §8.7.2.3.1 spells out what the recommended
 * per-page NSR leaves alone — "without affecting macros, DRCS, texture
 * definitions, color map or the currently displayed image" — and §8.7.1.4's
 * Support Record exists precisely to define macros other pages then invoke.
 * A caller therefore chooses where the resets fall: reset_decoder() is the
 * receiver's general reset (power-up or channel change, CEA-516 §8.5), and
 * run() executes one record from wherever the state currently stands.
 *
 * The record is a program (CEA-516 §6.1, ANSI X3.110-1983), and running it
 * means walking the byte stream while three things change underneath: which
 * G-set a byte belongs to (NaplpsCodeEnvironment), what the presentation
 * attributes are (NaplpsState), and where the drawing point is. Each drawing
 * operation is emitted with the attributes that were in force when it ran, so
 * the display list needs none of that state to be re-drawn.
 *
 * What is deliberately not done here is rasterisation. The primitives carry
 * their control points, their logical pel, their texture and their colours in
 * unit space; turning an arc into pixels, choosing a font, or stepping a
 * hatching pattern is the renderer's business. That keeps the MVP boundary
 * where the teletext viewer's is and keeps this testable against the standard
 * rather than against a screenshot.
 *
 * Two limits are enforced rather than assumed, because a recovered record has
 * lost packets in it and a decoder that trusts its input will hang on one:
 * macro nesting (kNaplpsMaxMacroDepth) and the shared storage budget
 * (kNaplpsSharedStorageBytes). Both are counted in the diagnostics.
 */
class NaplpsInterpreter {
 public:
  NaplpsInterpreter();

  /**
   * @brief Decode for a receiver of the given resolution
   *
   * Almost nothing the interpreter does depends on the receiver — it resolves
   * the presentation into unit-space geometry and leaves rendering to a
   * consumer. DRCS is the exception: §6.2.3 sizes a downloadable character's
   * storage buffer from "the minimum physical resolution ... covered by the
   * character field", so the buffer a definition is captured into is as coarse
   * or as fine as the receiver is.
   */
  explicit NaplpsInterpreter(NaplpsRenderGrid grid);

  /// Change the receiver resolution. Affects DRCS definitions read after it,
  /// which is why a caller sets it before running any presentation code.
  void set_render_grid(NaplpsRenderGrid grid) { state_.set_render_grid(grid); }

  /**
   * @brief The general reset of CEA-516 §8.5
   *
   * "On power-up or TV-channel change ... A general reset is equivalent to a
   * NSR, followed by a RESET command with two operands consisting of all 1s" —
   * everything to its default, macros, DRCS, texture masks and colour map
   * included. Call before the first record of an independent presentation;
   * do not call between records that share state (a Support Record and the
   * pages that need it).
   */
  void reset_decoder();

  /**
   * @brief The Caption Record preset of CEA-516 §5.2.7.3
   *
   * A receiver executing a caption record "first executes a Non-Selective
   * Reset (NSR)" and is then left with the cursor and drawing point at (0,0),
   * map entries 0, 1 and 7 loaded with transparent, black and white, colour
   * mode 2 drawing white over black, and the code environment at its defaults.
   * Call after reset_decoder() (and after any Support Record), before running
   * a record whose Caption Flag is set.
   */
  void apply_caption_state();

  /**
   * @brief Run |record| and return what it drew
   *
   * @param record Record data as CEA-516 §5.3 delivers it — the record header
   *               already removed, byte parity still in place
   *
   * Byte parity is stripped here rather than by the caller: §3.3 puts odd
   * parity in b8 of every data byte of a type-zero group, and a NAPLPS byte is
   * its low seven bits. A caller passing already-stripped bytes gets the same
   * result, since stripping twice is idempotent.
   *
   * Never throws. A record that runs out mid-sequence yields the primitives it
   * managed, with the shortfall in the diagnostics.
   *
   * Persistent state — macros, DRCS, texture masks, the colour map and the
   * presentation attributes — is carried in from previous runs and left in
   * place afterwards; only the per-record transients (the display list, open
   * definitions, execution frames) start fresh. The snapshot's colour map,
   * DRCS set and texture masks therefore include whatever earlier records
   * defined, which is what a page drawn against a Support Record needs.
   *
   * |keep_display| carries the previous run's display list in as the starting
   * canvas, which is how a More Record is presented: CEA-516 §5.2.7.8 has it
   * "presented after the completion of the presentation of the current
   * Record", drawing over what is already on screen rather than over a
   * cleared one — the record's own CS or RESET is what clears, when it wants
   * to. The carried primitives keep their colour map addresses, so a map
   * write in the continuation retro-colours them exactly as it would the
   * pixels of a real display. Diagnostics always describe this record alone.
   */
  NabtsPageSnapshot run(const std::vector<uint8_t>& record,
                        bool keep_display = false);

  /// The state after the last run(), for a test that wants to inspect it.
  const NaplpsState& state() const { return state_; }

 private:
  /// Where bytes are being executed from: the record, or an expanding macro.
  struct Frame {
    const uint8_t* bytes = nullptr;
    size_t length = 0;
    size_t position = 0;
  };

  /// Where a definition in progress is collecting its bytes.
  enum class Collecting : uint8_t {
    kNothing,
    /// DEF MACRO / DEFT MACRO: stored, not executed (§6.2.2.1, §6.2.2.3).
    kMacro,
    /// DEFP MACRO: stored *and* executed (§6.2.2.2).
    kMacroExecuting,
    /// DEF DRCS: executed into a character buffer (§6.2.3).
    kDrcs,
    /// DEF TEXTURE: executed into a mask buffer (§6.2.4).
    kTextureMask,
  };

  // ---- The main loop -------------------------------------------------------

  /// Execute one byte from the top frame. Returns false when the frame is done.
  bool step();

  void execute_byte(uint8_t byte);
  void execute_c0(uint8_t byte);
  void execute_escape();
  void execute_c1(NaplpsC1 control);
  void execute_graphic(uint8_t byte);

  /// The reset half of NSR (§6.1.6.5 items 1-5): code environment, DOMAIN,
  /// text parameters and the active field, TEXTURE attributes, and colour mode
  /// 0 with a white drawing colour. The colour map, programmable masks, macros
  /// and DRCS are left alone — which is what makes them persist between pages.
  void apply_nsr_reset();

  /// Drop everything drawn so far and, where |colour| is what the display was
  /// cleared to, record the clear as a full-display-area filled rectangle so a
  /// renderer shows the cleared colour rather than its own canvas.
  /// |map_address| is the colour's map address, or -1 for a direct colour;
  /// nominal black with no address emits nothing, black being what a renderer
  /// shows for an empty list anyway.
  void clear_display(const NabtsColour& colour, int map_address);

  // ---- PDI ----------------------------------------------------------------

  /**
   * @brief Execute the PDI opcode |opcode| with the numeric data that follows
   *
   * The operand bytes are gathered first, because §5.3.2.2.5 makes the count
   * decide the meaning — short is zero-extended, long repeats the opcode.
   * §5.3.1 defines the run's end: "A PDI sequence is terminated by an opcode
   * introducing the next PDI sequence or by any other presentation layer code
   * not from the numeric data section of the same PDI set", with the
   * transparent controls of §6.1.4-6.1.6.1 explicitly not terminating it.
   */
  void execute_pdi(uint8_t opcode);

  /// Collect the numeric data bytes following the current position.
  std::vector<uint8_t> gather_operands();

  void pdi_reset(NaplpsOperandReader& reader);
  void pdi_domain(NaplpsOperandReader& reader);
  void pdi_text(NaplpsOperandReader& reader);
  void pdi_texture(NaplpsOperandReader& reader);
  void pdi_set_colour(NaplpsOperandReader& reader, size_t operand_bytes);
  void pdi_select_colour(NaplpsOperandReader& reader, size_t operand_bytes);
  void pdi_blink(NaplpsOperandReader& reader, size_t operand_bytes);
  void pdi_wait(NaplpsOperandReader& reader);
  void pdi_point(NaplpsPdi opcode, NaplpsOperandReader& reader);
  void pdi_line(NaplpsPdi opcode, NaplpsOperandReader& reader);
  void pdi_arc(NaplpsPdi opcode, NaplpsOperandReader& reader);
  void pdi_rect(NaplpsPdi opcode, NaplpsOperandReader& reader);
  void pdi_poly(NaplpsPdi opcode, NaplpsOperandReader& reader);
  void pdi_field(NaplpsOperandReader& reader);
  void pdi_incremental(NaplpsPdi opcode, NaplpsOperandReader& reader,
                       const std::vector<uint8_t>& operands);

  // ---- Emission ------------------------------------------------------------

  /// A primitive with the current attributes already filled in.
  NabtsPrimitive make_primitive(NabtsPrimitiveKind kind) const;

  /// Emit |primitive|, into the display list or into whatever definition buffer
  /// is collecting — §6.2.3 and §6.2.4 both redirect drawing rather than
  /// suppressing it.
  void emit(NabtsPrimitive primitive);

  /// Move the drawing point to |point|, taking the cursor with it if the move
  /// attribute says to (§5.3.2.3.7).
  void move_drawing_point(const NabtsPoint& point);
  /// Move the cursor, taking the drawing point with it if the move attribute
  /// says to.
  void move_cursor(const NabtsPoint& point);

  /// |point| resolved into the unit screen, counting a clamp if it needed one.
  NabtsPoint resolve(NabtsPoint point);
  /// |base| displaced by |delta|, resolved.
  NabtsPoint resolve_relative(const NabtsPoint& base, const NabtsPoint& delta);

  /// A cursor movement expressed relative to the character path, which is what
  /// §6.1.2 defines the four format effectors in terms of.
  enum class CursorMove : uint8_t {
    kForward,   ///< APF (§6.1.2.2), and the advance after a character
    kBackward,  ///< APB (§6.1.2.1): 180 degrees from the path
    kDown,      ///< APD (§6.1.2.3): -90 degrees, by the interrow spacing
    kUp,        ///< APU (§6.1.2.5): +90 degrees, by the interrow spacing
  };

  /// Move the cursor one step of |move| (§6.1.2), wrapping to the next row
  /// where §5.3.2.3.6 says a forward step should.
  void move_cursor_by(CursorMove move);

  /**
   * @brief §5.3.2.3.6's suppression of the explicit APR APD after an automatic
   *        one
   *
   * "If an explicit APR APD (or APD APR) sequence is received after an
   * automatic APR APD is executed but before the character field origin is
   * moved, aligned, or set by any other received command or sequence, the
   * explicit APR APD (or APD APR) sequence shall be executed as a null
   * operation." A service writes every line flush to its field and ends it
   * with CR LF; without this rule each exactly-full line gains a blank row and
   * everything below it lands on top of the page's positioned elements.
   */
  enum class WrapSuppress : uint8_t {
    kNone,    ///< No automatic APR APD outstanding.
    kArmed,   ///< An automatic APR APD ran; an explicit pair would be null.
    kSawApr,  ///< First half (APR) of the suppressed pair consumed.
    kSawApd,  ///< First half (APD) of the suppressed pair consumed.
  };

  /**
   * @brief Whether a move to |point| leaves the cursor on the row it is on
   *
   * Measured across the character path, since that is the direction a row
   * advance is in (§5.3.2.3.5) — a move along the path is a move within the
   * row however the path is turned.
   *
   * §5.3.2.3.6 closes its suppression window when the character field origin is
   * "moved, aligned, or set by any other received command". A drawing-point
   * command that sets the left margin of the row the cursor is already on has
   * not left that row, and the row advance the window stands for is still
   * outstanding — so the window survives it. Reading the clause to cover that
   * too double-spaces every line a service indents, and indenting is how the
   * services in hand write a paragraph: carriage return, set the indent, line
   * feed. Anything that moves the origin to another row closes the window, as
   * does displaying a character, which is what tells a service's own line
   * ending from a real one further down the line.
   */
  bool stays_on_row(const NabtsPoint& point) const;

  /// Advance the cursor one character along the character path (§5.3.2.3.3).
  void advance_cursor() { move_cursor_by(CursorMove::kForward); }

  // ---- Definitions ---------------------------------------------------------

  /// Start collecting for |what| at code |code|.
  void begin_definition(Collecting what, uint8_t code);
  /// Finish whatever is being collected.
  void end_definition();

  /// Rasterise |primitive| into the definition buffer, which is what §6.2.3
  /// means by "all drawing operations affect the DRCS storage buffer rather
  /// than the display area".
  void draw_into_definition(const NabtsPrimitive& primitive);

  /// Invoke the macro at |code| by pushing a frame for it.
  void invoke_macro(uint8_t code);

  NaplpsCodeEnvironment env_;
  NaplpsState state_;
  NabtsPageSnapshot snapshot_;

  /// Execution frames: the record at the bottom, expanding macros above it.
  std::vector<Frame> frames_;
  /// The record's own bytes, parity stripped, which frames_[0] points into.
  std::vector<uint8_t> record_;

  Collecting collecting_ = Collecting::kNothing;
  /// Bytes gathered for a macro definition.
  std::vector<uint8_t> definition_body_;
  uint8_t definition_code_ = 0;
  bool definition_is_transmit_ = false;
  /// Frame the open definition is collecting from. Bytes executed from deeper
  /// frames — a macro expanding inside a DEFP MACRO body — are executed but not
  /// stored, which is §6.2.2.1's "storage of that reference only, not the
  /// expansion".
  size_t defining_frame_index_ = 0;
  /// Whether any presentation layer code has arrived inside the open DRCS
  /// definition. §6.2.3 frees the character's buffer when a definition is
  /// terminated with none.
  bool definition_had_code_ = false;
  /// The DRCS character or texture mask being written into.
  NabtsDrcsCharacter* drcs_target_ = nullptr;
  NabtsTextureMask* mask_target_ = nullptr;

  /// §6.2.3: the last DRCS code defined, so a DEF DRCS that terminates another
  /// takes "the next character of the DRCS G-set (ie, in the circular sequence
  /// 2/0, 2/1, ... 7/15, 2/0 ...)".
  uint8_t last_drcs_code_ = kNaplpsLastCode;
  bool have_last_drcs_code_ = false;

  /// The last graphic byte executed, which REPEAT (§6.2.7.2) repeats.
  uint8_t last_graphic_ = 0;
  bool have_last_graphic_ = false;

  WrapSuppress wrap_suppress_ = WrapSuppress::kNone;
};

}  // namespace tbc::vbi

#endif  // TBC_VBI_NAPLPS_INTERPRETER_H
