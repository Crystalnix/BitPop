/*
 * Copyright © 2009,2010  Red Hat, Inc.
 * Copyright © 2010,2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#include "hb-ot-shape-private.hh"
#include "hb-ot-shape-complex-private.hh"

#include "hb-font-private.hh"



hb_tag_t common_features[] = {
  HB_TAG('c','c','m','p'),
  HB_TAG('l','o','c','l'),
  HB_TAG('m','a','r','k'),
  HB_TAG('m','k','m','k'),
  HB_TAG('r','l','i','g'),
};

hb_tag_t horizontal_features[] = {
  HB_TAG('c','a','l','t'),
  HB_TAG('c','l','i','g'),
  HB_TAG('c','u','r','s'),
  HB_TAG('k','e','r','n'),
  HB_TAG('l','i','g','a'),
};

/* Note:
 * Technically speaking, vrt2 and vert are mutually exclusive.
 * According to the spec, valt and vpal are also mutually exclusive.
 * But we apply them all for now.
 */
hb_tag_t vertical_features[] = {
  HB_TAG('v','a','l','t'),
  HB_TAG('v','e','r','t'),
  HB_TAG('v','k','r','n'),
  HB_TAG('v','p','a','l'),
  HB_TAG('v','r','t','2'),
};

static void
hb_ot_shape_collect_features (hb_ot_shape_planner_t          *planner,
			      const hb_segment_properties_t  *props,
			      const hb_feature_t             *user_features,
			      unsigned int                    num_user_features)
{
  switch (props->direction) {
    case HB_DIRECTION_LTR:
      planner->map.add_bool_feature (HB_TAG ('l','t','r','a'));
      planner->map.add_bool_feature (HB_TAG ('l','t','r','m'));
      break;
    case HB_DIRECTION_RTL:
      planner->map.add_bool_feature (HB_TAG ('r','t','l','a'));
      planner->map.add_bool_feature (HB_TAG ('r','t','l','m'), false);
      break;
    case HB_DIRECTION_TTB:
    case HB_DIRECTION_BTT:
    case HB_DIRECTION_INVALID:
    default:
      break;
  }

#define ADD_FEATURES(array) \
  HB_STMT_START { \
    for (unsigned int i = 0; i < ARRAY_LENGTH (array); i++) \
      planner->map.add_bool_feature (array[i]); \
  } HB_STMT_END

  hb_ot_shape_complex_collect_features (planner->shaper, &planner->map, props);

  ADD_FEATURES (common_features);

  if (HB_DIRECTION_IS_HORIZONTAL (props->direction))
    ADD_FEATURES (horizontal_features);
  else
    ADD_FEATURES (vertical_features);

#undef ADD_FEATURES

  for (unsigned int i = 0; i < num_user_features; i++) {
    const hb_feature_t *feature = &user_features[i];
    planner->map.add_feature (feature->tag, feature->value, (feature->start == 0 && feature->end == (unsigned int) -1));
  }
}


static void
hb_ot_shape_setup_masks (hb_ot_shape_context_t *c)
{
  hb_mask_t global_mask = c->plan->map.get_global_mask ();
  c->buffer->reset_masks (global_mask);

  hb_ot_shape_complex_setup_masks (c->plan->shaper, &c->plan->map, c->buffer);

  for (unsigned int i = 0; i < c->num_user_features; i++)
  {
    const hb_feature_t *feature = &c->user_features[i];
    if (!(feature->start == 0 && feature->end == (unsigned int)-1)) {
      unsigned int shift;
      hb_mask_t mask = c->plan->map.get_mask (feature->tag, &shift);
      c->buffer->set_masks (feature->value << shift, mask, feature->start, feature->end);
    }
  }
}


/* Main shaper */

/* Prepare */

void
_hb_set_unicode_props (hb_buffer_t *buffer)
{
  unsigned int count = buffer->len;
  for (unsigned int i = 1; i < count; i++)
    hb_glyph_info_set_unicode_props (&buffer->info[i], buffer->unicode);
}

static void
hb_form_clusters (hb_buffer_t *buffer)
{
  unsigned int count = buffer->len;
  for (unsigned int i = 1; i < count; i++)
    if (FLAG (buffer->info[i].general_category()) &
	(FLAG (HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK) |
	 FLAG (HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK) |
	 FLAG (HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK)))
      buffer->info[i].cluster = buffer->info[i - 1].cluster;
}

static void
hb_ensure_native_direction (hb_buffer_t *buffer)
{
  hb_direction_t direction = buffer->props.direction;

  /* TODO vertical:
   * The only BTT vertical script is Ogham, but it's not clear to me whether OpenType
   * Ogham fonts are supposed to be implemented BTT or not.  Need to research that
   * first. */
  if ((HB_DIRECTION_IS_HORIZONTAL (direction) && direction != hb_script_get_horizontal_direction (buffer->props.script)) ||
      (HB_DIRECTION_IS_VERTICAL   (direction) && direction != HB_DIRECTION_TTB))
  {
    hb_buffer_reverse_clusters (buffer);
    buffer->props.direction = HB_DIRECTION_REVERSE (buffer->props.direction);
  }
}


/* Substitute */

static void
hb_mirror_chars (hb_ot_shape_context_t *c)
{
  hb_unicode_funcs_t *unicode = c->buffer->unicode;

  if (HB_DIRECTION_IS_FORWARD (c->target_direction))
    return;

  hb_mask_t rtlm_mask = c->plan->map.get_1_mask (HB_TAG ('r','t','l','m'));

  unsigned int count = c->buffer->len;
  for (unsigned int i = 0; i < count; i++) {
    hb_codepoint_t codepoint = hb_unicode_mirroring (unicode, c->buffer->info[i].codepoint);
    if (likely (codepoint == c->buffer->info[i].codepoint))
      c->buffer->info[i].mask |= rtlm_mask; /* XXX this should be moved to before setting user-feature masks */
    else
      c->buffer->info[i].codepoint = codepoint;
  }
}

static void
hb_map_glyphs (hb_font_t    *font,
	       hb_buffer_t  *buffer)
{
  hb_codepoint_t glyph;

  if (unlikely (!buffer->len))
    return;

  buffer->clear_output ();

  unsigned int count = buffer->len - 1;
  for (buffer->idx = 0; buffer->idx < count;) {
    if (unlikely (is_variation_selector (buffer->info[buffer->idx + 1].codepoint))) {
      hb_font_get_glyph (font, buffer->info[buffer->idx].codepoint, buffer->info[buffer->idx + 1].codepoint, &glyph);
      buffer->replace_glyph (glyph);
      buffer->skip_glyph ();
    } else {
      hb_font_get_glyph (font, buffer->info[buffer->idx].codepoint, 0, &glyph);
      buffer->replace_glyph (glyph);
    }
  }
  if (likely (buffer->idx < buffer->len)) {
    hb_font_get_glyph (font, buffer->info[buffer->idx].codepoint, 0, &glyph);
    buffer->replace_glyph (glyph);
  }
  buffer->swap_buffers ();
}

static void
hb_substitute_default (hb_ot_shape_context_t *c)
{
  hb_ot_layout_substitute_start (c->buffer);

  hb_mirror_chars (c);

  hb_map_glyphs (c->font, c->buffer);
}

static void
hb_ot_substitute_complex (hb_ot_shape_context_t *c)
{
  if (hb_ot_layout_has_substitution (c->face)) {
    c->plan->map.substitute (c->face, c->buffer);
    c->applied_substitute_complex = TRUE;
  }

  hb_ot_layout_substitute_finish (c->buffer);

  return;
}

static void
hb_substitute_complex_fallback (hb_ot_shape_context_t *c HB_UNUSED)
{
  /* TODO Arabic */
}


/* Position */

static void
hb_position_default (hb_ot_shape_context_t *c)
{
  hb_ot_layout_position_start (c->buffer);

  unsigned int count = c->buffer->len;
  for (unsigned int i = 0; i < count; i++) {
    hb_font_get_glyph_advance_for_direction (c->font, c->buffer->info[i].codepoint,
					     c->buffer->props.direction,
					     &c->buffer->pos[i].x_advance,
					     &c->buffer->pos[i].y_advance);
    hb_font_subtract_glyph_origin_for_direction (c->font, c->buffer->info[i].codepoint,
						 c->buffer->props.direction,
						 &c->buffer->pos[i].x_offset,
						 &c->buffer->pos[i].y_offset);
  }
}

static void
hb_ot_position_complex (hb_ot_shape_context_t *c)
{

  if (hb_ot_layout_has_positioning (c->face))
  {
    /* Change glyph origin to what GPOS expects, apply GPOS, change it back. */

    unsigned int count = c->buffer->len;
    for (unsigned int i = 0; i < count; i++) {
      hb_font_add_glyph_origin_for_direction (c->font, c->buffer->info[i].codepoint,
					      HB_DIRECTION_LTR,
					      &c->buffer->pos[i].x_offset,
					      &c->buffer->pos[i].y_offset);
    }

    c->plan->map.position (c->font, c->buffer);

    for (unsigned int i = 0; i < count; i++) {
      hb_font_subtract_glyph_origin_for_direction (c->font, c->buffer->info[i].codepoint,
						   HB_DIRECTION_LTR,
						   &c->buffer->pos[i].x_offset,
						   &c->buffer->pos[i].y_offset);
    }

    c->applied_position_complex = TRUE;
  }

  hb_ot_layout_position_finish (c->buffer);

  return;
}

static void
hb_position_complex_fallback (hb_ot_shape_context_t *c HB_UNUSED)
{
  /* TODO Mark pos */
}

static void
hb_truetype_kern (hb_ot_shape_context_t *c)
{
  /* TODO Check for kern=0 */
  unsigned int count = c->buffer->len;
  for (unsigned int i = 1; i < count; i++) {
    hb_position_t x_kern, y_kern, kern1, kern2;
    hb_font_get_glyph_kerning_for_direction (c->font,
					     c->buffer->info[i - 1].codepoint, c->buffer->info[i].codepoint,
					     c->buffer->props.direction,
					     &x_kern, &y_kern);

    kern1 = x_kern >> 1;
    kern2 = x_kern - kern1;
    c->buffer->pos[i - 1].x_advance += kern1;
    c->buffer->pos[i].x_advance += kern2;
    c->buffer->pos[i].x_offset += kern2;

    kern1 = y_kern >> 1;
    kern2 = y_kern - kern1;
    c->buffer->pos[i - 1].y_advance += kern1;
    c->buffer->pos[i].y_advance += kern2;
    c->buffer->pos[i].y_offset += kern2;
  }
}

static void
hb_position_complex_fallback_visual (hb_ot_shape_context_t *c)
{
  hb_truetype_kern (c);
}


/* Do it! */

static void
hb_ot_shape_execute_internal (hb_ot_shape_context_t *c)
{
  c->buffer->deallocate_var_all ();

  /* Save the original direction, we use it later. */
  c->target_direction = c->buffer->props.direction;

  HB_BUFFER_ALLOCATE_VAR (c->buffer, general_category);
  HB_BUFFER_ALLOCATE_VAR (c->buffer, combining_class);

  _hb_set_unicode_props (c->buffer); /* BUFFER: Set general_category and combining_class */

  hb_form_clusters (c->buffer);

  hb_ensure_native_direction (c->buffer);

  _hb_ot_shape_normalize (c);

  hb_ot_shape_setup_masks (c);

  /* SUBSTITUTE */
  {
    hb_substitute_default (c);

    hb_ot_substitute_complex (c);

    if (!c->applied_substitute_complex)
      hb_substitute_complex_fallback (c);
  }

  /* POSITION */
  {
    hb_position_default (c);

    hb_ot_position_complex (c);

    hb_bool_t position_fallback = !c->applied_position_complex;
    if (position_fallback)
      hb_position_complex_fallback (c);

    if (HB_DIRECTION_IS_BACKWARD (c->buffer->props.direction))
      hb_buffer_reverse (c->buffer);

    if (position_fallback)
      hb_position_complex_fallback_visual (c);
  }

  HB_BUFFER_DEALLOCATE_VAR (c->buffer, combining_class);
  HB_BUFFER_DEALLOCATE_VAR (c->buffer, general_category);

  c->buffer->props.direction = c->target_direction;

  c->buffer->deallocate_var_all ();
}

static void
hb_ot_shape_plan_internal (hb_ot_shape_plan_t       *plan,
			   hb_face_t                *face,
			   const hb_segment_properties_t  *props,
			   const hb_feature_t       *user_features,
			   unsigned int              num_user_features)
{
  hb_ot_shape_planner_t planner;

  planner.shaper = hb_ot_shape_complex_categorize (props);

  hb_ot_shape_collect_features (&planner, props, user_features, num_user_features);

  planner.compile (face, props, *plan);
}

static void
hb_ot_shape_execute (hb_ot_shape_plan_t *plan,
		     hb_font_t          *font,
		     hb_buffer_t        *buffer,
		     const hb_feature_t *user_features,
		     unsigned int        num_user_features)
{
  hb_ot_shape_context_t c = {plan, font, font->face, buffer, user_features, num_user_features};
  hb_ot_shape_execute_internal (&c);
}

hb_bool_t
hb_ot_shape (hb_font_t          *font,
	     hb_buffer_t        *buffer,
	     const hb_feature_t *features,
	     unsigned int        num_features,
	     const char * const *shaper_options)
{
  hb_ot_shape_plan_t plan;

  buffer->guess_properties ();

  hb_ot_shape_plan_internal (&plan, font->face, &buffer->props, features, num_features);
  hb_ot_shape_execute (&plan, font, buffer, features, num_features);

  return TRUE;
}


