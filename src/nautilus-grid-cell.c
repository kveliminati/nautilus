/*
 * Copyright (C) 2022 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-grid-cell.h"

#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-icon-info.h"
#include "nautilus-image.h"
#include "nautilus-tag-manager.h"
#include "nautilus-thumbnails.h"
#include "nautilus-ui-utilities.h"
#include "nautilus-view-item.h"
#include "nautilus-view-cell.h"

struct _NautilusGridCell
{
    NautilusViewCell parent_instance;

    GSignalGroup *item_signal_group;

    GQuark *caption_attributes;

    GtkWidget *icon;
    GtkWidget *emblems_box;
    GtkWidget *labels_box;
    GtkWidget *label;
    GtkWidget *first_caption;
    GtkWidget *second_caption;
    GtkWidget *third_caption;

    gboolean in_file_change;

    /* Small icon with the name beside it, instead of below it. */
    gboolean compact;
};

G_DEFINE_TYPE (NautilusGridCell, nautilus_grid_cell, NAUTILUS_TYPE_VIEW_CELL)

static void
update_icon (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_cut;

    g_return_if_fail (item != NULL);

    g_object_get (item, "is-cut", &is_cut, NULL);

    if (is_cut)
    {
        gtk_widget_set_visible (self->icon, FALSE);
        gtk_widget_remove_css_class (self->icon, "hidden-file");

        return;
    }

    g_autoptr (GdkPaintable) icon_paintable = NULL;
    NautilusFile *file = nautilus_view_item_get_file (item);
    guint icon_size;
    gint scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
    NautilusFileIconFlags flags = NAUTILUS_FILE_ICON_FLAGS_NONE;
    gboolean show_thumbnail = nautilus_file_should_show_thumbnail (file);

    g_object_get (self, "icon-size", &icon_size, NULL);
    icon_paintable = nautilus_file_get_icon_paintable (file, icon_size, scale_factor, flags);
    gtk_widget_set_visible (self->icon, TRUE);
    nautilus_image_set_size (NAUTILUS_IMAGE (self->icon), icon_size);
    nautilus_image_set_fallback (NAUTILUS_IMAGE (self->icon), icon_paintable);

    if (self->in_file_change ||
        !show_thumbnail)
    {
        nautilus_image_set_source (NAUTILUS_IMAGE (self->icon), NULL);
    }

    if (show_thumbnail)
    {
        g_autoptr (GFile) location = nautilus_file_get_location (file);

        nautilus_image_set_source (NAUTILUS_IMAGE (self->icon), location);
    }

    if (nautilus_file_is_hidden_file (file))
    {
        gtk_widget_add_css_class (self->icon, "hidden-file");
    }
    else
    {
        gtk_widget_remove_css_class (self->icon, "hidden-file");
    }
}

static void
update_captions (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget * const caption_labels[] =
    {
        self->first_caption,
        self->second_caption,
        self->third_caption
    };
    G_STATIC_ASSERT (G_N_ELEMENTS (caption_labels) == NAUTILUS_GRID_CELL_N_CAPTIONS);

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    for (guint i = 0; i < NAUTILUS_GRID_CELL_N_CAPTIONS; i++)
    {
        GQuark attribute_q = self->caption_attributes[i];
        gboolean show_caption;

        show_caption = (attribute_q != 0 && !self->compact);
        gtk_widget_set_visible (caption_labels[i], show_caption);
        if (show_caption)
        {
            g_autofree gchar *string = NULL;
            string = nautilus_file_get_string_attribute_q (file, attribute_q);
            gtk_label_set_text (GTK_LABEL (caption_labels[i]), string);
        }
    }
}

static void
update_emblems (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget *child;
    GtkIconTheme *theme;
    g_autolist (GIcon) emblems = NULL;
    g_autofree gchar *file_uri = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    file_uri = nautilus_file_get_uri (file);

    /* Remove old emblems. */
    while ((child = gtk_widget_get_first_child (self->emblems_box)) != NULL)
    {
        gtk_box_remove (GTK_BOX (self->emblems_box), child);
    }

    if (nautilus_tag_manager_file_is_starred (nautilus_tag_manager_get (), file_uri))
    {
        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_icon_name ("starred-symbolic"));
    }

    theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    emblems = nautilus_file_get_emblem_icons (file);
    for (GList *l = emblems; l != NULL; l = l->next)
    {
        if (!gtk_icon_theme_has_gicon (theme, l->data))
        {
            g_autofree gchar *icon_string = g_icon_to_string (l->data);
            g_warning ("Failed to add emblem. “%s” not found in the icon theme",
                       icon_string);
            continue;
        }

        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_gicon (l->data));
    }
}

static void
on_file_changed (NautilusGridCell *self)
{
    self->in_file_change = TRUE;

    update_icon (self);
    update_emblems (self);
    update_captions (self);

    self->in_file_change = FALSE;
}

static void
on_icon_size_changed (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));

    if (item == NULL)
    {
        /* Cell is not bound to an item yet. Do nothing. */
        return;
    }

    update_icon (self);
    update_captions (self);
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

static gboolean
on_label_query_tooltip (GtkWidget  *widget,
                        int         x,
                        int         y,
                        gboolean    keyboard_tip,
                        GtkTooltip *tooltip,
                        gpointer    user_data)
{
    GtkLabel *label = GTK_LABEL (widget);

    if (pango_layout_is_ellipsized (gtk_label_get_layout (label)))
    {
        gtk_tooltip_set_text (tooltip, gtk_label_get_text (label));
        return TRUE;
    }

    return FALSE;
}

static void
on_starred_changed (NautilusTagManager *tag_manager,
                    GList              *changed_files,
                    gpointer            user_data)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (user_data);
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    if (item == NULL)
    {
        return;
    }

    file = nautilus_view_item_get_file (item);
    if (g_list_find (changed_files, file))
    {
        update_emblems (self);
    }
}

static void
nautilus_grid_cell_dispose (GObject *object)
{
    NautilusGridCell *self = (NautilusGridCell *) object;

    gtk_widget_dispose_template (GTK_WIDGET (self), NAUTILUS_TYPE_GRID_CELL);
    g_clear_object (&self->item_signal_group);

    G_OBJECT_CLASS (nautilus_grid_cell_parent_class)->dispose (object);
}

#define EMBLEMS_BOX_WIDTH 18
#define VERTICAL_PADDING 6
#define COMPACT_SPACING 6

static void
compact_measure (NautilusGridCell *self,
                 GtkOrientation    orientation,
                 int               for_size,
                 int              *minimum,
                 int              *natural)
{
    guint icon_size;
    int labels_min, labels_nat, emblems_min, emblems_nat;

    g_object_get (self, "icon-size", &icon_size, NULL);

    gtk_widget_measure (self->emblems_box, orientation, -1,
                        &emblems_min, &emblems_nat, NULL, NULL);

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        gtk_widget_measure (self->labels_box, orientation, -1,
                            &labels_min, &labels_nat, NULL, NULL);
        /* GtkGridView sizes columns from the items' minimum width, so ask
         * for the whole name; the label's max-width-chars caps long ones. */
        *minimum = *natural = icon_size + COMPACT_SPACING + labels_nat + emblems_nat;
    }
    else /* GTK_ORIENTATION_VERTICAL */
    {
        gtk_widget_measure (self->labels_box, orientation, -1,
                            &labels_min, &labels_nat, NULL, NULL);
        *minimum = MAX ((int) icon_size, MAX (labels_min, emblems_min));
        *natural = MAX ((int) icon_size, MAX (labels_nat, emblems_nat));
    }
}

static void
compact_size_allocate (NautilusGridCell *self,
                       int               width,
                       int               height)
{
    GtkAllocation icon_allocation, emblems_allocation, labels_allocation;
    gboolean ltr = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_LTR;
    guint size;
    int icon_size, emblems_width;

    g_object_get (self, "icon-size", &size, NULL);
    icon_size = size;
    gtk_widget_measure (self->emblems_box, GTK_ORIENTATION_HORIZONTAL, -1,
                        NULL, &emblems_width, NULL, NULL);

    /* [icon] [labels.........] [emblems], mirrored for RTL. */
    icon_allocation = (GtkAllocation) {
        ltr ? 0 : width - icon_size, (height - icon_size) / 2,
        icon_size, icon_size
    };
    emblems_allocation = (GtkAllocation) {
        ltr ? width - emblems_width : 0, 0,
        emblems_width, height
    };
    labels_allocation = (GtkAllocation) {
        ltr ? icon_size + COMPACT_SPACING : emblems_width, 0,
        MAX (0, width - icon_size - COMPACT_SPACING - emblems_width), height
    };

    gtk_widget_size_allocate (self->icon, &icon_allocation, -1);
    gtk_widget_size_allocate (self->emblems_box, &emblems_allocation, -1);
    gtk_widget_size_allocate (self->labels_box, &labels_allocation, -1);
}

static void
nautilus_grid_cell_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *min_baseline,
                            int            *nat_baseline)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (widget);
    guint icon_size;
    int width, child_min, child_nat;

    if (self->compact)
    {
        compact_measure (self, orientation, for_size, minimum, natural);
        *min_baseline = *nat_baseline = -1;
        return;
    }

    g_object_get (self, "icon-size", &icon_size, NULL);
    width = EMBLEMS_BOX_WIDTH + icon_size + EMBLEMS_BOX_WIDTH;

    /* We always expect our layout to fit into this width. Check that
     * this is indeed the case. This also shuts up the
     *     Allocating size to ... without calling gtk_widget_measure().
     *     How does the code know the size to allocate?
     * warning that we get in debug builds of GTK otherwise. The answer
     * to that question is: we know that the icon is a GtkPicture, and
     * we set the icon_paintable of the specific size on it.
     */
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        gtk_widget_measure (self->labels_box, orientation, -1,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > width))
        {
            g_warning ("%s %p unexpectedly doesn't fit into width of %d",
                       gtk_widget_get_name (self->labels_box), self->labels_box, width);
            width = child_min;
        }
        gtk_widget_measure (self->icon, orientation, -1,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > (int) icon_size))
        {
            g_warning ("%s %p unexpectedly doesn't fit into width of %d",
                       gtk_widget_get_name (self->icon), self->icon, icon_size);
            width += child_min - icon_size;
        }
        gtk_widget_measure (self->emblems_box, orientation, -1,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > EMBLEMS_BOX_WIDTH))
        {
            g_warning ("%s %p unexpectedly doesn't fit into width of %d",
                       gtk_widget_get_name (self->emblems_box), self->emblems_box, EMBLEMS_BOX_WIDTH);
            width += 2 * (child_min - EMBLEMS_BOX_WIDTH);
        }
    }
    else /* GTK_ORIENTATION_VERTICAL */
    {
        gtk_widget_measure (self->icon, orientation, icon_size,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > (int) icon_size))
        {
            g_warning ("%s %p unexpectedly doesn't fit into height of %d",
                       gtk_widget_get_name (self->icon), self->icon, icon_size);
        }
        gtk_widget_measure (self->emblems_box, orientation, EMBLEMS_BOX_WIDTH,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > (int) icon_size))
        {
            g_warning ("%s %p unexpectedly doesn't fit into height of %d",
                       gtk_widget_get_name (self->emblems_box), self->emblems_box, icon_size);
        }
    }

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        *natural = *minimum = width;
        *min_baseline = *nat_baseline = -1;
        return;
    }

    gtk_widget_measure (self->labels_box, GTK_ORIENTATION_VERTICAL, width,
                        &child_min, &child_nat,
                        min_baseline, nat_baseline);

    *minimum = icon_size + VERTICAL_PADDING + child_min;
    *natural = icon_size + VERTICAL_PADDING + child_nat;
    if (*min_baseline != -1)
    {
        *min_baseline += icon_size + VERTICAL_PADDING;
    }
    if (*nat_baseline != -1)
    {
        *nat_baseline += icon_size + VERTICAL_PADDING;
    }
}

static void
nautilus_grid_cell_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (widget);
    GtkAllocation child_allocation;
    guint icon_size;

    if (self->compact)
    {
        compact_size_allocate (self, width, height);
        return;
    }

    g_object_get (self, "icon-size", &icon_size, NULL);

    /* Put the icon at the top. Center it horizontally, with
     * EMBLEMS_BOX_WIDTH horizontal margins on both sides.
     * Note that the icon is expected to center itself further
     * inside the allocation that we give it.
     */
    child_allocation = (GtkAllocation) {
        EMBLEMS_BOX_WIDTH, 0,
        width - EMBLEMS_BOX_WIDTH * 2, icon_size
    };
    gtk_widget_size_allocate (self->icon, &child_allocation, -1);

    /* Put the emblems_box into the "end" margin. */
    child_allocation.width = EMBLEMS_BOX_WIDTH;
    if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
    {
        child_allocation.x = width - EMBLEMS_BOX_WIDTH;
    }
    else
    {
        child_allocation.x = 0;
    }
    gtk_widget_size_allocate (self->emblems_box, &child_allocation, -1);

    /* Give the remaining space to labels box. */
    child_allocation = (GtkAllocation) {
        0, icon_size + VERTICAL_PADDING,
        width, height - (icon_size + VERTICAL_PADDING)
    };
    if (baseline != -1)
    {
        baseline -= icon_size + VERTICAL_PADDING;
    }
    gtk_widget_size_allocate (self->labels_box, &child_allocation, baseline);
}

static void
snapshot (GtkWidget   *widget,
          GtkSnapshot *snapshot)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (widget);
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_cut;

    g_object_get (item, "is-cut", &is_cut, NULL);

    if (is_cut)
    {
        AdwStyleManager *style_manager = adw_style_manager_get_default ();
        gboolean is_high_contrast = adw_style_manager_get_high_contrast (style_manager);
        guint icon_size;
        graphene_rect_t dash_bounds, icon_bounds;
        GdkRGBA color, dashed_border_color, icon_color;
        const double border_opacity = is_high_contrast ? 0.5 : 0.15;
        const double dim_opacity = is_high_contrast ? 0.9 : 0.55;

        g_object_get (self, "icon-size", &icon_size, NULL);
        if (self->compact)
        {
            graphene_rect_t icon_rect;

            if (!gtk_widget_compute_bounds (self->icon, widget, &icon_rect))
            {
                graphene_rect_init (&icon_rect, 0, 0, icon_size, icon_size);
            }
            dash_bounds = GRAPHENE_RECT_INIT (icon_rect.origin.x, icon_rect.origin.y, icon_size, icon_size);
        }
        else
        {
            dash_bounds = GRAPHENE_RECT_INIT (EMBLEMS_BOX_WIDTH, 0, icon_size, icon_size);
        }
        gtk_widget_get_color (widget, &color);

        /* The compact layout's smallest icon sizes (16, 24) have no dashed
         * border style, so just draw the cut icon there, like the list view. */
        if (icon_size >= NAUTILUS_THUMBNAIL_MINIMUM_ICON_SIZE)
        {
            graphene_rect_inset_r (&dash_bounds, 0.2 * icon_size, 0.2 * icon_size, &icon_bounds);
            dashed_border_color = color;
            dashed_border_color.alpha *= border_opacity;
            nautilus_ui_draw_icon_dashed_border (snapshot, &dash_bounds, dashed_border_color);
        }
        else
        {
            icon_bounds = dash_bounds;
        }

        icon_color = color;
        icon_color.alpha *= dim_opacity;
        nautilus_ui_draw_symbolic_icon (snapshot,
                                        icon_size <= NAUTILUS_LIST_ICON_SIZE_MEDIUM ? "cut-symbolic" : "cut-large-symbolic",
                                        &icon_bounds,
                                        icon_color,
                                        gtk_widget_get_scale_factor (widget));
    }

    GTK_WIDGET_CLASS (nautilus_grid_cell_parent_class)->snapshot (widget, snapshot);
}

static void
nautilus_grid_cell_class_init (NautilusGridCellClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = nautilus_grid_cell_dispose;

    widget_class->measure = nautilus_grid_cell_measure;
    widget_class->size_allocate = nautilus_grid_cell_size_allocate;

    widget_class->snapshot = snapshot;

    g_type_ensure (NAUTILUS_TYPE_IMAGE);

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-grid-cell.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, icon);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, emblems_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, labels_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, label);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, first_caption);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, second_caption);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, third_caption);

    gtk_widget_class_bind_template_callback (widget_class, on_label_query_tooltip);

    gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_NONE);
}

static void
nautilus_grid_cell_init (NautilusGridCell *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    g_signal_connect (self, "notify::icon-size",
                      G_CALLBACK (on_icon_size_changed), NULL);
    g_signal_connect (self, "notify::scale-factor", G_CALLBACK (on_icon_size_changed), NULL);

    g_signal_connect_object (nautilus_tag_manager_get (), "starred-changed",
                             G_CALLBACK (on_starred_changed), self, G_CONNECT_DEFAULT);

    g_signal_connect_object (nautilus_preferences, "changed::" NAUTILUS_PREFERENCES_DATE_TIME_FORMAT,
                             G_CALLBACK (update_captions), self,
                             G_CONNECT_SWAPPED);

    /* Connect automatically to an item. */
    self->item_signal_group = g_signal_group_new (NAUTILUS_TYPE_VIEW_ITEM);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::is-cut",
                                    (GCallback) update_icon, self);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::is-cut",
                                    (GCallback) gtk_widget_queue_draw, self);
    g_signal_group_connect_swapped (self->item_signal_group, "file-changed",
                                    (GCallback) on_file_changed, self);
    g_signal_connect_object (self->item_signal_group, "bind",
                             (GCallback) on_file_changed, self,
                             G_CONNECT_SWAPPED);

    g_object_bind_property (self, "item",
                            self->item_signal_group, "target",
                            G_BINDING_SYNC_CREATE);
}

NautilusGridCell *
nautilus_grid_cell_new (NautilusListBase *view)
{
    return g_object_new (NAUTILUS_TYPE_GRID_CELL,
                         "view", view,
                         NULL);
}

void
nautilus_grid_cell_set_caption_attributes (NautilusGridCell *self,
                                           GQuark           *attrs)
{
    self->caption_attributes = attrs;
}

void
nautilus_grid_cell_set_compact (NautilusGridCell *self,
                                gboolean          compact)
{
    GtkLabel *label = GTK_LABEL (self->label);

    if (self->compact == compact)
    {
        return;
    }

    self->compact = compact;

    /* One line, left-aligned name, like a list row. */
    gtk_label_set_wrap (label, !compact);
    gtk_label_set_lines (label, compact ? 1 : 3);
    gtk_label_set_ellipsize (label, compact ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_justify (label, compact ? GTK_JUSTIFY_LEFT : GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign (label, compact ? 0.0 : 0.5);
    gtk_label_set_max_width_chars (label, compact ? 40 : -1);
    gtk_widget_set_valign (self->labels_box, compact ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self->emblems_box),
                                    compact ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_valign (self->emblems_box, compact ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);
    gtk_widget_set_valign (GTK_WIDGET (self), compact ? GTK_ALIGN_CENTER : GTK_ALIGN_START);

    if (compact)
    {
        gtk_widget_add_css_class (GTK_WIDGET (self), "compact");
    }
    else
    {
        gtk_widget_remove_css_class (GTK_WIDGET (self), "compact");
    }

    gtk_widget_queue_resize (GTK_WIDGET (self));
}
