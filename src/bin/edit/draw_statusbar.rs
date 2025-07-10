// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

use edit::arena::scratch_arena;
use edit::framebuffer::{Attributes, IndexedColor};
use edit::fuzzy::score_fuzzy;
use edit::helpers::*;
use edit::input::vk;
use edit::tui::*;
use edit::{arena_format, icu};

use crate::localization::*;
use crate::state::*;

pub fn draw_statusbar(ctx: &mut Context, state: &mut State) {
    ctx.table_begin("statusbar");
    ctx.attr_focus_well();
    ctx.attr_background_rgba(state.menubar_color_bg);
    ctx.attr_foreground_rgba(state.menubar_color_fg);
    ctx.table_set_cell_gap(Size { width: 2, height: 0 });
    ctx.attr_intrinsic_size(Size { width: COORD_TYPE_SAFE_MAX, height: 1 });
    ctx.attr_padding(Rect::two(0, 1));

    if let Some(doc) = state.documents.active() {
        let mut tb = doc.buffer.borrow_mut();

        ctx.table_next_row();

        if ctx.button("newline", if tb.is_crlf() { "CRLF" } else { "LF" }, ButtonStyle::default()) {
            let is_crlf = tb.is_crlf();
            tb.normalize_newlines(!is_crlf);
        }
        if state.wants_statusbar_focus {
            state.wants_statusbar_focus = false;
            ctx.steal_focus();
        }

        state.wants_encoding_picker |=
            ctx.button("encoding", tb.encoding(), ButtonStyle::default());
        if state.wants_encoding_picker {
            if doc.path.is_some() {
                ctx.block_begin("frame");
                ctx.attr_float(FloatSpec {
                    anchor: Anchor::Last,
                    gravity_x: 0.0,
                    gravity_y: 1.0,
                    offset_x: 0.0,
                    offset_y: 0.0,
                });
                ctx.attr_padding(Rect::two(0, 1));
                ctx.attr_border();
                {
                    if ctx.button("reopen", loc(LocId::EncodingReopen), ButtonStyle::default()) {
                        state.wants_encoding_change = StateEncodingChange::Reopen;
                    }
                    ctx.focus_on_first_present();
                    if ctx.button("convert", loc(LocId::EncodingConvert), ButtonStyle::default()) {
                        state.wants_encoding_change = StateEncodingChange::Convert;
                    }
                }
                ctx.block_end();
            } else {
                // Can't reopen a file that doesn't exist.
                state.wants_encoding_change = StateEncodingChange::Convert;
            }

            if !ctx.contains_focus() {
                state.wants_encoding_picker = false;
                ctx.needs_rerender();
            }
        }

        state.wants_indentation_picker |= ctx.button(
            "indentation",
            &arena_format!(
                ctx.arena(),
                "{}:{}",
                loc(if tb.indent_with_tabs() {
                    LocId::IndentationTabs
                } else {
                    LocId::IndentationSpaces
                }),
                tb.tab_size(),
            ),
            ButtonStyle::default(),
        );
        if state.wants_indentation_picker {
            ctx.table_begin("indentation-picker");
            ctx.attr_float(FloatSpec {
                anchor: Anchor::Last,
                gravity_x: 0.0,
                gravity_y: 1.0,
                offset_x: 0.0,
                offset_y: 0.0,
            });
            ctx.attr_border();
            ctx.attr_padding(Rect::two(0, 1));
            ctx.table_set_cell_gap(Size { width: 1, height: 0 });
            {
                if ctx.contains_focus() && ctx.consume_shortcut(vk::RETURN) {
                    ctx.toss_focus_up();
                }

                ctx.table_next_row();

                ctx.list_begin("type");
                ctx.focus_on_first_present();
                ctx.attr_padding(Rect::two(0, 1));
                {
                    if ctx.list_item(tb.indent_with_tabs(), loc(LocId::IndentationTabs))
                        != ListSelection::Unchanged
                    {
                        tb.set_indent_with_tabs(true);
                        ctx.needs_rerender();
                    }
                    if ctx.list_item(!tb.indent_with_tabs(), loc(LocId::IndentationSpaces))
                        != ListSelection::Unchanged
                    {
                        tb.set_indent_with_tabs(false);
                        ctx.needs_rerender();
                    }
                }
                ctx.list_end();

                ctx.list_begin("width");
                ctx.attr_padding(Rect::two(0, 2));
                {
                    for width in 1u8..=8 {
                        let ch = [b'0' + width];
                        let label = unsafe { std::str::from_utf8_unchecked(&ch) };

                        if ctx.list_item(tb.tab_size() == width as CoordType, label)
                            != ListSelection::Unchanged
                        {
                            tb.set_tab_size(width as CoordType);
                            ctx.needs_rerender();
                        }
                    }
                }
                ctx.list_end();
            }
            ctx.table_end();

            if !ctx.contains_focus() {
                state.wants_indentation_picker = false;
                ctx.needs_rerender();
            }
        }

        ctx.label(
            "location",
            &arena_format!(
                ctx.arena(),
                "{}:{}",
                tb.cursor_logical_pos().y + 1,
                tb.cursor_logical_pos().x + 1
            ),
        );

        #[cfg(feature = "debug-latency")]
        ctx.label(
            "stats",
            &arena_format!(ctx.arena(), "{}/{}", tb.logical_line_count(), tb.visual_line_count(),),
        );

        if tb.is_overtype() && ctx.button("overtype", "OVR", ButtonStyle::default()) {
            tb.set_overtype(false);
            ctx.needs_rerender();
        }

        if tb.is_dirty() {
            ctx.label("dirty", "*");
        }

        ctx.block_begin("filename-container");
        ctx.attr_intrinsic_size(Size { width: COORD_TYPE_SAFE_MAX, height: 1 });
        {
            let total = state.documents.len();
            let mut filename = doc.filename.as_str();
            let filename_buf;

            if total > 1 {
                filename_buf = arena_format!(ctx.arena(), "{} + {}", filename, total - 1);
                filename = &filename_buf;
            }

            state.wants_go_to_file |= ctx.button("filename", filename, ButtonStyle::default());
            ctx.inherit_focus();
            ctx.attr_overflow(Overflow::TruncateMiddle);
            ctx.attr_position(Position::Right);
        }
        ctx.block_end();
    } else {
        state.wants_statusbar_focus = false;
        state.wants_encoding_picker = false;
        state.wants_indentation_picker = false;
    }

    ctx.table_end();
}

pub fn draw_dialog_encoding_change(ctx: &mut Context, state: &mut State) {
    let encoding = state.documents.active_mut().map_or("", |doc| doc.buffer.borrow().encoding());
    let reopen = state.wants_encoding_change == StateEncodingChange::Reopen;
    let width = (ctx.size().width - 20).max(10);
    let height = (ctx.size().height - 10).max(10);
    let mut change = None;
    let mut done = encoding.is_empty();

    ctx.modal_begin(
        "encode",
        if reopen { loc(LocId::EncodingReopen) } else { loc(LocId::EncodingConvert) },
    );
    {
        ctx.table_begin("encoding-search");
        ctx.table_set_columns(&[0, COORD_TYPE_SAFE_MAX]);
        ctx.table_set_cell_gap(Size { width: 1, height: 0 });
        ctx.inherit_focus();
        {
            ctx.table_next_row();
            ctx.inherit_focus();

            ctx.label("needle-label", loc(LocId::SearchNeedleLabel));

            if ctx.editline("needle", &mut state.encoding_picker_needle) {
                encoding_picker_update_list(state);
            }
            ctx.inherit_focus();
        }
        ctx.table_end();

        ctx.scrollarea_begin("scrollarea", Size { width, height });
        ctx.attr_background_rgba(ctx.indexed_alpha(IndexedColor::Black, 1, 4));
        {
            ctx.list_begin("encodings");
            ctx.inherit_focus();

            for enc in state
                .encoding_picker_results
                .as_deref()
                .unwrap_or_else(|| icu::get_available_encodings().preferred)
            {
                if ctx.list_item(enc.canonical == encoding, enc.label) == ListSelection::Activated {
                    change = Some(enc.canonical);
                    break;
                }
                ctx.attr_overflow(Overflow::TruncateTail);
            }
            ctx.list_end();
        }
        ctx.scrollarea_end();
    }
    done |= ctx.modal_end();
    done |= change.is_some();

    if let Some(encoding) = change
        && let Some(doc) = state.documents.active_mut()
    {
        if reopen && doc.path.is_some() {
            let mut res = Ok(());
            if doc.buffer.borrow().is_dirty() {
                res = doc.save(None);
            }
            if res.is_ok() {
                res = doc.reread(Some(encoding));
            }
            if let Err(err) = res {
                error_log_add(ctx, state, err);
            }
        } else {
            doc.buffer.borrow_mut().set_encoding(encoding);
        }
    }

    if done {
        state.wants_encoding_change = StateEncodingChange::None;
        state.encoding_picker_needle.clear();
        state.encoding_picker_results = None;
        ctx.needs_rerender();
    }
}

fn encoding_picker_update_list(state: &mut State) {
    state.encoding_picker_results = None;

    let needle = state.encoding_picker_needle.trim_ascii();
    if needle.is_empty() {
        return;
    }

    let encodings = icu::get_available_encodings();
    let scratch = scratch_arena(None);
    let mut matches = Vec::new_in(&*scratch);

    for enc in encodings.all {
        let local_scratch = scratch_arena(Some(&scratch));
        let (score, _) = score_fuzzy(&local_scratch, enc.label, needle, true);

        if score > 0 {
            matches.push((score, *enc));
        }
    }

    matches.sort_by(|a, b| b.0.cmp(&a.0));
    state.encoding_picker_results = Some(Vec::from_iter(matches.iter().map(|(_, enc)| *enc)));
}

pub fn draw_go_to_file(ctx: &mut Context, state: &mut State) {
    ctx.modal_begin("go-to-file", loc(LocId::ViewGoToFile));
    {
        let width = (ctx.size().width - 20).max(10);
        let height = (ctx.size().height - 10).max(10);

        ctx.scrollarea_begin("scrollarea", Size { width, height });
        ctx.attr_background_rgba(ctx.indexed_alpha(IndexedColor::Black, 1, 4));
        ctx.inherit_focus();
        {
            ctx.list_begin("documents");
            ctx.inherit_focus();

            if state.documents.update_active(|doc| {
                let tb = doc.buffer.borrow();

                ctx.styled_list_item_begin();
                ctx.attr_overflow(Overflow::TruncateTail);
                ctx.styled_label_add_text(if tb.is_dirty() { "* " } else { "  " });
                ctx.styled_label_add_text(&doc.filename);

                if let Some(path) = &doc.dir {
                    ctx.styled_label_add_text("   ");
                    ctx.styled_label_set_attributes(Attributes::Italic);
                    ctx.styled_label_add_text(path.as_str());
                }

                ctx.styled_list_item_end(false) == ListSelection::Activated
            }) {
                state.wants_go_to_file = false;
                ctx.needs_rerender();
            }

            ctx.list_end();
        }
        ctx.scrollarea_end();
    }
    if ctx.modal_end() {
        state.wants_go_to_file = false;
    }
}
