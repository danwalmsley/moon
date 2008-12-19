/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * textbox.cpp: 
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <cairo.h>

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "textbox.h"
#include "utils.h"
#include "dependencyproperty.h"
#include "validators.h"


static SolidColorBrush *default_selection_background_brush = NULL;
static SolidColorBrush *default_selection_foreground_brush = NULL;
static SolidColorBrush *default_background_brush = NULL;
static SolidColorBrush *default_foreground_brush = NULL;

static Brush *
default_selection_background (void)
{
	if (!default_selection_background_brush)
		default_selection_background_brush = new SolidColorBrush ("black");
	
	return (Brush *) default_selection_background_brush;
}

static Brush *
default_selection_foreground (void)
{
	if (!default_selection_foreground_brush)
		default_selection_foreground_brush = new SolidColorBrush ("white");
	
	return (Brush *) default_selection_foreground_brush;
}

static Brush *
default_background (void)
{
	if (!default_background_brush)
		default_background_brush = new SolidColorBrush ("white");
	
	return (Brush *) default_background_brush;
}

static Brush *
default_foreground (void)
{
	if (!default_foreground_brush)
		default_foreground_brush = new SolidColorBrush ("black");
	
	return (Brush *) default_foreground_brush;
}

void
textbox_shutdown (void)
{
	if (default_selection_background_brush) {
		default_selection_background_brush->unref ();
		default_selection_background_brush = NULL;
	}
	
	if (default_selection_foreground_brush) {
		default_selection_foreground_brush->unref ();
		default_selection_foreground_brush = NULL;
	}
	
	if (default_background_brush) {
		default_background_brush->unref ();
		default_background_brush = NULL;
	}
	
	if (default_foreground_brush) {
		default_foreground_brush->unref ();
		default_foreground_brush = NULL;
	}
}


//
// TextBuffer
//

#define UNICODE_LEN(size) (sizeof (gunichar) * (size))
#define UNICODE_OFFSET(buf,offset) (((char *) buf) + sizeof (gunichar) * (offset))

class TextBuffer {
	int allocated;
	
	void Resize (int needed)
	{
		bool resize = false;
		
		if (allocated >= needed + 128) {
			while (allocated >= needed + 128)
				allocated -= 128;
			resize = true;
		} else if (allocated < needed) {
			while (allocated < needed)
				allocated += 128;
			resize = true;
		}
		
		if (resize)
			text = (gunichar *) g_realloc (text, UNICODE_LEN (allocated));
	}
	
 public:
	gunichar *text;
	int len;
	
	TextBuffer ()
	{
		text = NULL;
		Reset ();
	}
	
	void Reset ()
	{
		text = (gunichar *) g_realloc (text, UNICODE_LEN (128));
		allocated = 128;
		text[0] = '\0';
		len = 0;
	}
	
	void Append (gunichar c)
	{
		Resize (len + 1);
		
		text[len++] = c;
		text[len] = 0;
	}
	
	void Append (const gunichar *str, int count)
	{
		Resize (len + count + 1);
		
		memcpy (UNICODE_OFFSET (text, len), str, UNICODE_LEN (count));
		len += count;
		text[len] = 0;
	}
	
	void Cut (int start, int length)
	{
		int offset;
		
		if (length == 0 || start >= len)
			return;
		
		offset = MAX (len, start + length);
		
		memmove (UNICODE_OFFSET (text, start), UNICODE_OFFSET (text, offset), UNICODE_LEN ((len - offset) + 1));
	}
	
	void Insert (int index, gunichar c)
	{
		Resize (len + 1);
		
		if (index < len) {
			// shift all chars beyond position @index down by 1 char
			memmove (UNICODE_OFFSET (text, index + 1), UNICODE_OFFSET (text, index), UNICODE_LEN ((len - index) + 1));
			text[index] = c;
			len++;
		} else {
			text[len++] = c;
			text[len] = 0;
		}
	}
	
	void Insert (int index, const gunichar *str, int count)
	{
		Resize (len + count + 1);
		
		if (index < len) {
			// shift all chars beyond position @index down by @count chars
			memmove (UNICODE_OFFSET (text, index + count), UNICODE_OFFSET (text, index), UNICODE_LEN ((len - index) + 1));
			
			// insert @count chars of @str into our buffer at position @index
			memcpy (UNICODE_OFFSET (text, index), str, UNICODE_LEN (count));
			len += count;
		} else {
			// simply append @count chars of @str onto the end of our buffer
			memcpy (UNICODE_OFFSET (text, len), str, UNICODE_LEN (count));
			len += count;
			text[len] = 0;
		}
	}
	
	void Prepend (gunichar c)
	{
		Resize (len + 1);
		
		// shift the entire buffer down by 1 char
		memmove (UNICODE_OFFSET (text, 1), text, UNICODE_LEN (len + 1));
		text[0] = c;
		len++;
	}
	
	void Prepend (const gunichar *str, int count)
	{
		Resize (len + count + 1);
		
		// shift the endtire buffer down by @count chars
		memmove (UNICODE_OFFSET (text, count), text, UNICODE_LEN (len + 1));
		
		// copy @count chars of @str into the beginning of our buffer
		memcpy (text, str, UNICODE_LEN (count));
		len += count;
	}
	
	void Replace (int start, int length, const gunichar *str, int count)
	{
		int beyond;
		
		if (start > len) {
			g_warning ("TextBuffer::Replace() start out of range");
			return;
		}
		
		if (start + length > len) {
			g_warning ("TextBuffer::Replace() length out of range");
			return;
		}
		
		// Check for the easy cases first...
		if (length == 0) {
			Insert (start, str, count);
			return;
		} else if (count == 0) {
			Cut (start, length);
			return;
		} else if (count == length) {
			memcpy (UNICODE_OFFSET (text, start), str, UNICODE_LEN (count));
			return;
		}
		
		Resize ((len - length) + count + 1);
		
		// calculate the number of chars beyond @start that won't be cut
		beyond = len - (start + length);
		
		// shift all chars beyond position (@start + length) into position...
		memmove (UNICODE_OFFSET (text, start + count), UNICODE_OFFSET (text, start + length), UNICODE_LEN (beyond + 1));
		
		// copy @count chars of @str into our buffer at position @start
		memcpy (UNICODE_OFFSET (text, start), str, UNICODE_LEN (count));
		
		len = (len - length) + count;
	}
};


//
// TextBox
//

TextBox::TextBox ()
{
	static bool init = true;
	if (init) {
		init = false;
		TextBox::MaxLengthProperty->SetValueValidator (Validators::PositiveIntValidator);
		TextBox::SelectionLengthProperty->SetValueValidator (Validators::PositiveIntValidator);
		TextBox::SelectionStartProperty->SetValueValidator (Validators::PositiveIntValidator);
	}
	/* initialize the font description and layout */
	hints = new TextLayoutHints (TextAlignmentLeft, LineStackingStrategyMaxHeight, 0.0);
	layout = new TextLayout ();
	
	font = new TextFontDescription ();
	font->SetFamily (CONTROL_FONT_FAMILY);
	font->SetStretch (CONTROL_FONT_STRETCH);
	font->SetWeight (CONTROL_FONT_WEIGHT);
	font->SetStyle (CONTROL_FONT_STYLE);
	font->SetSize (CONTROL_FONT_SIZE);
	
	buffer = new TextBuffer ();
	
	selection.background = default_selection_background ();
	selection.foreground = default_selection_foreground ();
	selection.length = 0;
	selection.start = 0;
	maxlen = 0;
	caret = 0;
	
	dirty = true;
}

TextBox::~TextBox ()
{
	delete buffer;
	delete layout;
	delete hints;
	delete font;
}

void
TextBox::Render (cairo_t *cr, int x, int y, int width, int height)
{
	if (dirty)
		Layout (cr);
	
	cairo_save (cr);
	cairo_set_matrix (cr, &absolute_xform);
	Paint (cr);
	cairo_restore (cr);
}

void
TextBox::GetSizeForBrush (cairo_t *cr, double *width, double *height)
{
	*height = GetActualHeight ();
	*width = GetActualWidth ();
}

void
TextBox::Layout (cairo_t *cr)
{
	double width = GetWidth ();
	List *runs;
	
	layout->SetWrapping (GetTextWrapping ());
	
	if (width > 0.0f)
		layout->SetMaxWidth (width);
	else
		layout->SetMaxWidth (-1.0);
	
	runs = new List ();
	runs->Append (new TextRun (buffer->text, buffer->len, TextDecorationsNone, font, NULL));
	
	layout->SetTextRuns (runs);
	layout->Layout (hints);
	
	dirty = false;
}

void
TextBox::Paint (cairo_t *cr)
{
	Brush *fg;
	
	printf ("TextBox::Paint()\n");
	
	if (!(fg = GetForeground ()))
		fg = default_foreground ();
	
	layout->Render (cr, GetOriginPoint (), Point (), hints, fg, &selection);
}

void
TextBox::OnPropertyChanged (PropertyChangedEventArgs *args)
{
	bool invalidate = true;
	
	if (args->property == Control::FontFamilyProperty) {
		char *family = args->new_value ? args->new_value->AsString () : NULL;
		font->SetFamily (family);
		dirty = true;
	} else if (args->property == Control::FontSizeProperty) {
		double size = args->new_value->AsDouble ();
		font->SetSize (size);
		dirty = true;
	} else if (args->property == Control::FontStretchProperty) {
		FontStretches stretch = (FontStretches) args->new_value->AsInt32 ();
		font->SetStretch (stretch);
		dirty = true;
	} else if (args->property == Control::FontStyleProperty) {
		FontStyles style = (FontStyles) args->new_value->AsInt32 ();
		font->SetStyle (style);
		dirty = true;
	} else if (args->property == Control::FontWeightProperty) {
		FontWeights weight = (FontWeights) args->new_value->AsInt32 ();
		font->SetWeight (weight);
		dirty = true;
	} else if (args->property == TextBox::AcceptsReturnProperty) {
		// No layout or rendering changes needed
		invalidate = false;
	} else if (args->property == TextBox::MaxLengthProperty) {
		/* FIXME: What happens if the current buffer length is > MaxLength? */
		maxlen = args->new_value->AsInt32 ();
	} else if (args->property == TextBox::SelectedTextProperty) {
		const char *str = args->new_value ? args->new_value->AsString () : "";
		gunichar *text;
		glong textlen;
		
		// FIXME: is the caret/selection updating logic correct?
		
		text = g_utf8_to_ucs4_fast (str, -1, &textlen);
		if (selection.start >= 0) {
			// replace the currently selected text
			buffer->Replace (selection.start, selection.length, text, textlen);
			caret = selection.start + textlen;
		} else {
			// insert the text at the caret position
			buffer->Insert (caret, text, textlen);
			caret += textlen;
		}
		
		selection.start = -1;
		selection.length = 0;
		g_free (text);
		dirty = true;
	} else if (args->property == TextBox::SelectionStartProperty) {
		selection.start = args->new_value->AsInt32 ();
		dirty = true;
	} else if (args->property == TextBox::SelectionLengthProperty) {
		selection.length = args->new_value->AsInt32 ();
		dirty = true;
	} else if (args->property == TextBox::SelectionBackgroundProperty) {
		if (!(selection.background = args->new_value ? args->new_value->AsBrush () : NULL))
			selection.background = default_selection_background ();
	} else if (args->property == TextBox::SelectionForegroundProperty) {
		if (!(selection.foreground = args->new_value ? args->new_value->AsBrush () : NULL))
			selection.foreground = default_selection_foreground ();
	} else if (args->property == TextBox::TextProperty) {
		const char *str = args->new_value ? args->new_value->AsString () : "";
		gunichar *text;
		glong textlen;
		
		// FIXME: is the caret/selection updating logic correct?
		
		text = g_utf8_to_ucs4_fast (str, -1, &textlen);
		buffer->Replace (0, buffer->len, text, textlen);
		selection.start = -1;
		selection.length = 0;
		caret = textlen;
		g_free (text);
		dirty = true;
	} else if (args->property == TextBox::TextAlignmentProperty) {
		// FIXME: we could probably avoid setting dirty=true
		// in this particular case depending on how we end up
		// implementing alignment in Layout::Render()
		hints->SetTextAlignment ((TextAlignment) args->new_value->AsInt32 ());
		dirty = true;
	} else if (args->property == TextBox::TextWrappingProperty) {
		dirty = true;
	} else if (args->property == TextBox::HorizontalScrollBarVisibilityProperty) {
		/* FIXME: need to figure out:
		 *
		 * 1. whether the scrollbar should be shown or not
		 * 2. whether the layout needs to be recalculated if the visibility changes
		 */
	} else if (args->property == TextBox::VerticalScrollBarVisibilityProperty) {
		/* FIXME: need to figure out:
		 *
		 * 1. whether the scrollbar should be shown or not
		 * 2. whether the layout needs to be recalculated if the visibility changes
		 */
	}
	
	if (invalidate) {
		if (dirty)
			UpdateBounds (true);
		
		Invalidate ();
	}
	
	if (args->property->GetOwnerType () != Type::TEXTBOX) {
		Control::OnPropertyChanged (args);
		// FIXME: does padding or anything else play a part in affecting textual wrapping?
		if (args->property == FrameworkElement::WidthProperty) {
			if (GetTextWrapping () != TextWrappingNoWrap)
				dirty = true;
			
			UpdateBounds (true);
		}
		
		return;
	}
	
	NotifyListenersOfPropertyChange (args);
}

void
TextBox::OnSubPropertyChanged (DependencyProperty *prop, DependencyObject *obj, PropertyChangedEventArgs *subobj_args)
{
	if (prop == TextBox::SelectionBackgroundProperty ||
	    prop == TextBox::SelectionForegroundProperty ||
	    prop == Control::BackgroundProperty ||
	    prop == Control::ForegroundProperty)
		Invalidate ();
	
	if (prop->GetOwnerType () != Type::TEXTBOX)
		Control::OnSubPropertyChanged (prop, obj, subobj_args);
}

Value *
TextBox::GetValue (DependencyProperty *property)
{
	return DependencyObject::GetValue (property);
}

Size
TextBox::ArrangeOverride (Size size)
{
	// FIXME: implement me
	return Control::ArrangeOverride (size);
}

void
TextBox::Select (int start, int length)
{
	if (start < 0 || start > buffer->len || start + length > buffer->len)
		return;
	
	SetSelectionStart (start);
	SetSelectionLength (length);
}

void
TextBox::SelectAll ()
{
	Select (0, buffer->len);
}

void
PasswordBox::OnPropertyChanged (PropertyChangedEventArgs *args)
{
	if (args->property == PasswordBox::PasswordProperty)
		Emit (PasswordBox::PasswordChangedEvent);
	
	TextBox::OnPropertyChanged (args);	
}

PasswordBox::PasswordBox ()
{
	static bool init = true;
	if (init) {
		init = false;
		PasswordBox::PasswordProperty->SetValueValidator (Validators::NonNullStringValidator);
		PasswordBox::MaxLengthProperty->SetValueValidator (Validators::PositiveIntValidator);
	}
}
