/* gb-editor-vim.c
 *
 * Copyright (C) 2014 Christian Hergert <christian@hergert.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "vim"

#include <glib/gi18n.h>
#include <gtksourceview/gtksource.h>

#include "gb-editor-vim.h"
#include "gb-log.h"

struct _GbEditorVimPrivate
{
  GtkTextView     *text_view;
  GbEditorVimMode  mode;
  guint            key_press_event_handler;
  guint            target_line_offset;
  guint            enabled : 1;
};

enum
{
  PROP_0,
  PROP_ENABLED,
  PROP_MODE,
  PROP_TEXT_VIEW,
  LAST_PROP
};

G_DEFINE_TYPE_WITH_PRIVATE (GbEditorVim, gb_editor_vim, G_TYPE_OBJECT)

static GParamSpec *gParamSpecs [LAST_PROP];

GbEditorVim *
gb_editor_vim_new (GtkTextView *text_view)
{
  g_return_val_if_fail (GTK_IS_TEXT_VIEW (text_view), NULL);

  return g_object_new (GB_TYPE_EDITOR_VIM,
                       "text-view", text_view,
                       NULL);
}

GbEditorVimMode
gb_editor_vim_get_mode (GbEditorVim *vim)
{
  g_return_val_if_fail (GB_IS_EDITOR_VIM (vim), 0);

  return vim->priv->mode;
}

static guint
gb_editor_vim_get_line_offset (GbEditorVim *vim)
{
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;

  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  return gtk_text_iter_get_line_offset (&iter);
}

static void
gb_editor_vim_set_mode (GbEditorVim     *vim,
                        GbEditorVimMode  mode)
{
  g_return_if_fail (GB_IS_EDITOR_VIM (vim));

  vim->priv->mode = mode;

  if (mode == GB_EDITOR_VIM_NORMAL)
    vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  g_object_notify_by_pspec (G_OBJECT (vim), gParamSpecs [PROP_MODE]);
}

static void
gb_editor_vim_move_line_start (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (priv->text_view));
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  gtk_text_buffer_get_iter_at_line (buffer, &iter,
                                    gtk_text_iter_get_line (&iter));

  while (!gtk_text_iter_ends_line (&iter) &&
         g_unichar_isspace (gtk_text_iter_get_char (&iter)))
    if (!gtk_text_iter_forward_char (&iter))
      break;

  gtk_text_buffer_select_range (buffer, &iter, &iter);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);
}

static void
gb_editor_vim_move_line_end (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  while (!gtk_text_iter_ends_line (&iter))
    if (!gtk_text_iter_forward_char (&iter))
      break;

  gtk_text_buffer_select_range (buffer, &iter, &iter);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);
}

static void
gb_editor_vim_move_backward (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;
  guint line;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);
  line = gtk_text_iter_get_line (&iter);

  /*
   * TODO: handle there being a selection.
   */

  if (gtk_text_iter_backward_char (&iter) &&
      (line == gtk_text_iter_get_line (&iter)))
    gtk_text_buffer_select_range (buffer, &iter, &iter);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);
}

static void
gb_editor_vim_move_backward_word (GbEditorVim *vim)
{
  GtkTextBuffer *buffer;
  GtkTextIter iter;
  GtkTextMark *insert;

  g_assert (GB_IS_EDITOR_VIM (vim));

  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  if (!gtk_text_iter_backward_word_start (&iter))
    gtk_text_buffer_get_start_iter (buffer, &iter);

  gtk_text_buffer_select_range (buffer, &iter, &iter);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_move_forward (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;
  guint line;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);
  line = gtk_text_iter_get_line (&iter);

  /*
   * TODO: handle there being a selection.
   */

  if (gtk_text_iter_forward_char (&iter) &&
      (line == gtk_text_iter_get_line (&iter)))
    gtk_text_buffer_select_range (buffer, &iter, &iter);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);
}

static void
gb_editor_vim_move_forward_word (GbEditorVim *vim)
{
  GtkTextBuffer *buffer;
  GtkTextIter iter;
  GtkTextMark *insert;

  g_assert (GB_IS_EDITOR_VIM (vim));

  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  /*
   * TODO: handle there being a selection.
   */

  if (!g_unichar_isspace (gtk_text_iter_get_char (&iter)) &&
      !gtk_text_iter_ends_word (&iter))
    if (!gtk_text_iter_forward_word_end (&iter))
      return;

  if (!gtk_text_iter_forward_word_end (&iter) ||
      !gtk_text_iter_backward_word_start (&iter))
    gtk_text_buffer_get_end_iter (buffer, &iter);

  gtk_text_buffer_select_range (buffer, &iter, &iter);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_move_down (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;
  guint line;
  guint offset;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);
  line = gtk_text_iter_get_line (&iter);
  offset = vim->priv->target_line_offset;

  /*
   * TODO: handle there being a selection.
   */

  gtk_text_buffer_get_iter_at_line (buffer, &iter, line + 1);
  if ((line + 1) == gtk_text_iter_get_line (&iter))
    {
      for (; offset; offset--)
        if (!gtk_text_iter_ends_line (&iter))
          if (!gtk_text_iter_forward_char (&iter))
            break;
      gtk_text_buffer_select_range (buffer, &iter, &iter);
    }

  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_move_up (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;
  guint line;
  guint offset;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);
  line = gtk_text_iter_get_line (&iter);
  offset = vim->priv->target_line_offset;

  if (line == 0)
    return;

  /*
   * TODO: handle there being a selection.
   */

  gtk_text_buffer_get_iter_at_line (buffer, &iter, line - 1);
  if ((line - 1) == gtk_text_iter_get_line (&iter))
    {
      for (; offset; offset--)
        if (!gtk_text_iter_ends_line (&iter))
          if (!gtk_text_iter_forward_char (&iter))
            break;
      gtk_text_buffer_select_range (buffer, &iter, &iter);
    }

  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_delete_selection (GbEditorVim *vim)
{
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (GB_IS_EDITOR_VIM (vim));

  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  gtk_text_buffer_get_selection_bounds (buffer, &begin, &end);

  /*
   * If there is no selection to delete, try to remove the next character
   * in the line. If there is no next character, delete the last character
   * in the line.
   */
  if (gtk_text_iter_equal (&begin, &end))
    {
      if (!gtk_text_iter_ends_line (&end))
        {
          if (!gtk_text_iter_forward_char (&end))
            return;
        }
      else if (!gtk_text_iter_starts_line (&begin))
        {
          if (!gtk_text_iter_backward_char (&begin))
            return;
        }
      else
        return;
    }

  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_delete (buffer, &begin, &end);
  gtk_text_buffer_end_user_action (buffer);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_select_line (GbEditorVim *vim)
{
  GbEditorVimPrivate *priv;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  buffer = gtk_text_view_get_buffer (priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  gtk_text_iter_assign (&begin, &iter);
  while (!gtk_text_iter_starts_line (&begin))
    if (!gtk_text_iter_backward_char (&begin))
      break;

  gtk_text_iter_assign (&end, &iter);
  while (!gtk_text_iter_ends_line (&end))
    if (!gtk_text_iter_forward_char (&end))
      break;

  gtk_text_buffer_select_range (buffer, &begin, &end);
}

static void
gb_editor_vim_undo (GbEditorVim *vim)
{
  GtkSourceUndoManager *undo;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;

  g_assert (GB_IS_EDITOR_VIM (vim));

  /*
   * We only support GtkSourceView for now.
   */
  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  if (!GTK_SOURCE_IS_BUFFER (buffer))
    return;

  undo = gtk_source_buffer_get_undo_manager (GTK_SOURCE_BUFFER (buffer));
  if (gtk_source_undo_manager_can_undo (undo))
    gtk_source_undo_manager_undo (undo);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_redo (GbEditorVim *vim)
{
  GtkSourceUndoManager *undo;
  GtkTextBuffer *buffer;
  GtkTextMark *insert;

  g_assert (GB_IS_EDITOR_VIM (vim));

  /*
   * We only support GtkSourceView for now.
   */
  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  if (!GTK_SOURCE_IS_BUFFER (buffer))
    return;

  undo = gtk_source_buffer_get_undo_manager (GTK_SOURCE_BUFFER (buffer));
  if (gtk_source_undo_manager_can_redo (undo))
    gtk_source_undo_manager_redo (undo);

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_insert_nl_before (GbEditorVim *vim)
{
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;
  guint line;

  g_assert (GB_IS_EDITOR_VIM (vim));

  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);
  line = gtk_text_iter_get_line (&iter);

  /*
   * Insert a newline before the current line.
   */
  gtk_text_buffer_get_iter_at_line (buffer, &iter, line);
  gtk_text_buffer_insert (buffer, &iter, "\n", 1);

  /*
   * Move ourselves back to the line we were one.
   */
  gtk_text_buffer_get_iter_at_line (buffer, &iter, line);

  /*
   * Select this position as the cursor.
   */
  gtk_text_buffer_select_range (buffer, &iter, &iter);

  /*
   * TODO: Query auto-indenter to see if we should indent the position.
   */

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static void
gb_editor_vim_insert_nl_after (GbEditorVim *vim)
{
  GtkTextBuffer *buffer;
  GtkTextMark *insert;
  GtkTextIter iter;

  g_assert (GB_IS_EDITOR_VIM (vim));

  buffer = gtk_text_view_get_buffer (vim->priv->text_view);
  insert = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &iter, insert);

  /*
   * Move to the end of the current line and insert a newline.
   */
  while (!gtk_text_iter_ends_line (&iter))
    if (!gtk_text_iter_forward_char (&iter))
      break;
  gtk_text_buffer_insert (buffer, &iter, "\n", 1);

  /*
   * Select this position as the cursor to update insert.
   */
  gtk_text_buffer_select_range (buffer, &iter, &iter);

  /*
   * TODO: Query auto-indenter to see if we should indent the position.
   */

  vim->priv->target_line_offset = gb_editor_vim_get_line_offset (vim);

  gtk_text_view_scroll_mark_onscreen (vim->priv->text_view, insert);
}

static gboolean
gb_editor_vim_handle_normal (GbEditorVim *vim,
                             GdkEventKey *event)
{
  g_assert (GB_IS_EDITOR_VIM (vim));
  g_assert (event);

  switch (event->keyval)
    {
    case GDK_KEY_I:
      /*
       * Start insert mode at the beginning of the line.
       */
      gb_editor_vim_move_line_start (vim);
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_INSERT);
      return TRUE;

    case GDK_KEY_i:
      /*
       * Start insert mode at the current line position.
       */
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_INSERT);
      return TRUE;

    case GDK_KEY_A:
      /*
       * Start insert mode at the end of the line.
       */
      gb_editor_vim_move_line_end (vim);
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_INSERT);
      return TRUE;

    case GDK_KEY_a:
      /*
       * Start insert mode at the beginning of the line.
       */
      gb_editor_vim_move_forward (vim);
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_INSERT);
      return TRUE;

    case GDK_KEY_l:
      /*
       * Move forward in the buffer one character, but stay on the
       * same line.
       */
      gb_editor_vim_move_forward (vim);
      return TRUE;

    case GDK_KEY_h:
      /*
       * Move backward in the buffer one character, but stay on the
       * same line.
       */
      gb_editor_vim_move_backward (vim);
      return TRUE;

    case GDK_KEY_j:
      /*
       * Move down in the buffer one line, and try to stay on the same column.
       */
      gb_editor_vim_move_down (vim);
      return TRUE;

    case GDK_KEY_k:
      /*
       * Move down in the buffer one line, and try to stay on the same column.
       */
      gb_editor_vim_move_up (vim);
      return TRUE;

    case GDK_KEY_V:
      /*
       * Select the current line.
       *
       * TODO: Allow this selection to grow. Might want to just add another
       *        mode for selections like VIM does.
       */
      gb_editor_vim_select_line (vim);
      return TRUE;

    case GDK_KEY_w:
      /*
       * Move forward by one word.
       */
      gb_editor_vim_move_forward_word (vim);
      return TRUE;

    case GDK_KEY_b:
      /*
       * Move backward by one word.
       */
      gb_editor_vim_move_backward_word (vim);
      return TRUE;

    case GDK_KEY_x:
      /*
       * Delete the current selection.
       */
      gb_editor_vim_delete_selection (vim);
      break;

    case GDK_KEY_u:
      /*
       * Undo the last operation if we can.
       */
      gb_editor_vim_undo (vim);
      break;

    case GDK_KEY_O:
      /*
       * Insert a newline before the current line, and start editing.
       */
      gb_editor_vim_insert_nl_before (vim);
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_INSERT);
      return TRUE;

    case GDK_KEY_o:
      /*
       * Insert a new line, and then begin insertion.
       */
      gb_editor_vim_insert_nl_after (vim);
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_INSERT);
      return TRUE;

    case GDK_KEY_r:
      /*
       * Try to redo a previously undone operation if we can.
       */
      if ((event->state & GDK_CONTROL_MASK))
        {
          gb_editor_vim_redo (vim);
          return TRUE;
        }

      break;

    default:
      break;
    }

  if (event->string && *event->string)
    return TRUE;

  return FALSE;
}

static gboolean
gb_editor_vim_handle_insert (GbEditorVim *vim,
                             GdkEventKey *event)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      gb_editor_vim_set_mode (vim, GB_EDITOR_VIM_NORMAL);
      return TRUE;
    }

  return FALSE;
}

static gboolean
gb_editor_vim_handle_command (GbEditorVim *vim,
                              GdkEventKey *event)
{
  return FALSE;
}

static gboolean
gb_editor_vim_key_press_event_cb (GtkTextView *text_view,
                                  GdkEventKey *event,
                                  GbEditorVim *vim)
{
  gboolean ret;

  ENTRY;

  g_return_val_if_fail (GTK_IS_TEXT_VIEW (text_view), FALSE);
  g_return_val_if_fail (event, FALSE);
  g_return_val_if_fail (GB_IS_EDITOR_VIM (vim), FALSE);

  switch (vim->priv->mode)
    {
    case GB_EDITOR_VIM_NORMAL:
      ret = gb_editor_vim_handle_normal (vim, event);
      RETURN (ret);

    case GB_EDITOR_VIM_INSERT:
      ret = gb_editor_vim_handle_insert (vim, event);
      RETURN (ret);

    case GB_EDITOR_VIM_COMMAND:
      ret = gb_editor_vim_handle_command (vim, event);
      RETURN (ret);

    default:
      g_assert_not_reached();
    }

  RETURN (FALSE);
}

static void
gb_editor_vim_connect (GbEditorVim *vim)
{
  g_return_if_fail (GB_IS_EDITOR_VIM (vim));

  vim->priv->key_press_event_handler =
    g_signal_connect (vim->priv->text_view,
                      "key-press-event",
                      G_CALLBACK (gb_editor_vim_key_press_event_cb),
                      vim);
}

static void
gb_editor_vim_disconnect (GbEditorVim *vim)
{
  g_return_if_fail (GB_IS_EDITOR_VIM (vim));

  g_signal_handler_disconnect (vim->priv->text_view,
                               vim->priv->key_press_event_handler);
  vim->priv->key_press_event_handler = 0;
}

gboolean
gb_editor_vim_get_enabled (GbEditorVim *vim)
{
  g_return_val_if_fail (GB_IS_EDITOR_VIM (vim), FALSE);

  return vim->priv->enabled;
}

void
gb_editor_vim_set_enabled (GbEditorVim *vim,
                           gboolean     enabled)
{
  GbEditorVimPrivate *priv;

  g_return_if_fail (GB_IS_EDITOR_VIM (vim));

  priv = vim->priv;

  if (priv->enabled == enabled)
    return;

  if (enabled)
    {
      gb_editor_vim_connect (vim);
      priv->enabled = TRUE;
    }
  else
    {
      gb_editor_vim_disconnect (vim);
      priv->enabled = FALSE;
    }

  g_object_notify_by_pspec (G_OBJECT (vim), gParamSpecs [PROP_ENABLED]);
}

GtkWidget *
gb_editor_vim_get_text_view (GbEditorVim *vim)
{
  g_return_val_if_fail (GB_IS_EDITOR_VIM (vim), NULL);

  return (GtkWidget *)vim->priv->text_view;
}

static void
gb_editor_vim_set_text_view (GbEditorVim *vim,
                             GtkTextView *text_view)
{
  GbEditorVimPrivate *priv;

  g_return_if_fail (GB_IS_EDITOR_VIM (vim));
  g_return_if_fail (GTK_IS_TEXT_VIEW (text_view));

  priv = vim->priv;

  if (priv->text_view == text_view)
    return;

  if (priv->text_view)
    {
      if (priv->enabled)
        gb_editor_vim_disconnect (vim);
      g_object_remove_weak_pointer (G_OBJECT (priv->text_view),
                                    (gpointer *)&priv->text_view);
      priv->text_view = NULL;
    }

  if (text_view)
    {
      priv->text_view = text_view;
      g_object_add_weak_pointer (G_OBJECT (text_view),
                                 (gpointer *)&priv->text_view);
      if (priv->enabled)
        gb_editor_vim_connect (vim);
    }

  g_object_notify_by_pspec (G_OBJECT (vim), gParamSpecs [PROP_TEXT_VIEW]);
}

static void
gb_editor_vim_finalize (GObject *object)
{
  GbEditorVimPrivate *priv = GB_EDITOR_VIM (object)->priv;

  if (priv->text_view)
    {
      g_object_remove_weak_pointer (G_OBJECT (priv->text_view),
                                    (gpointer *)&priv->text_view);
      priv->text_view = NULL;
    }

  G_OBJECT_CLASS (gb_editor_vim_parent_class)->finalize (object);
}

static void
gb_editor_vim_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GbEditorVim *vim = GB_EDITOR_VIM (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      g_value_set_boolean (value, gb_editor_vim_get_enabled (vim));
      break;

    case PROP_MODE:
      g_value_set_enum (value, gb_editor_vim_get_mode (vim));
      break;

    case PROP_TEXT_VIEW:
      g_value_set_object (value, gb_editor_vim_get_text_view (vim));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_editor_vim_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GbEditorVim *vim = GB_EDITOR_VIM (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      gb_editor_vim_set_enabled (vim, g_value_get_boolean (value));
      break;

    case PROP_TEXT_VIEW:
      gb_editor_vim_set_text_view (vim, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_editor_vim_class_init (GbEditorVimClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gb_editor_vim_finalize;
  object_class->get_property = gb_editor_vim_get_property;
  object_class->set_property = gb_editor_vim_set_property;

  gParamSpecs [PROP_ENABLED] =
    g_param_spec_boolean ("enabled",
                          _("Enabled"),
                          _("If the VIM engine is enabled."),
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_ENABLED,
                                   gParamSpecs [PROP_ENABLED]);

  gParamSpecs [PROP_MODE] =
    g_param_spec_enum ("mode",
                       _("Mode"),
                       _("The current mode of the widget."),
                       GB_TYPE_EDITOR_VIM_MODE,
                       GB_EDITOR_VIM_NORMAL,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_MODE,
                                   gParamSpecs [PROP_MODE]);

  gParamSpecs [PROP_TEXT_VIEW] =
    g_param_spec_object ("text-view",
                         _("Text View"),
                         _("The text view the VIM engine is managing."),
                         GTK_TYPE_TEXT_VIEW,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_TEXT_VIEW,
                                   gParamSpecs [PROP_TEXT_VIEW]);
}

static void
gb_editor_vim_init (GbEditorVim *vim)
{
  vim->priv = gb_editor_vim_get_instance_private (vim);
  vim->priv->enabled = FALSE;
  vim->priv->mode = GB_EDITOR_VIM_NORMAL;
}

GType
gb_editor_vim_mode_get_type (void)
{
  static GType type_id;
  static const GEnumValue values[] = {
    { GB_EDITOR_VIM_NORMAL, "GB_EDITOR_VIM_NORMAL", "NORMAL" },
    { GB_EDITOR_VIM_INSERT, "GB_EDITOR_VIM_INSERT", "INSERT" },
    { GB_EDITOR_VIM_COMMAND, "GB_EDITOR_VIM_COMMAND", "COMMAND" },
    { 0 }
  };

  if (!type_id)
    type_id = g_enum_register_static ("GbEditorVimMode", values);

  return type_id;
}