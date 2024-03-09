/* Procman - main window
 * Copyright (C) 2001 Kevin Vandersloot
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#include "interface.h"
#include "proctable.h"
#include "procactions.h"
#include "procdialogs.h"
#include "setaffinity.h"
#include "memmaps.h"
#include "openfiles.h"
#include "procproperties.h"
#include "load-graph.h"
#include "util.h"
#include "disks.h"
#include "settings-keys.h"
#include "legacy/gsm_color_button.h"

static const char*LOAD_GRAPH_CSS = "\
.loadgraph {\
    background: linear-gradient(to bottom,\
                  @window_bg_color,\
                  @view_bg_color);\
    color: @window_fg_color;\
}\
";

static void
search_text_changed (GtkEditable*,
                     gpointer data)
{
  GsmApplication * const app = static_cast<GsmApplication *>(data);

  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (gtk_tree_model_sort_get_model (
                                                           GTK_TREE_MODEL_SORT (gtk_tree_view_get_model (
                                                                                  GTK_TREE_VIEW (app->tree))))));
}

/*
static void
set_affinity_visiblity (GtkWidget *widget,
                        gpointer   user_data)
{
#ifndef __linux__
  GtkMenuItem *item = GTK_MENU_ITEM (widget);
  const gchar *name = gtk_menu_item_get_label (item);

  if (strcmp (name, "Set _Affinity") == 0)
    gtk_widget_set_visible (widget, false);
#endif
}*/

static void
create_proc_view (GsmApplication *app,
                  GtkBuilder     *builder)
{
  GsmTreeView *proctree;
  GtkWidget *scrolled;

  proctree = proctable_new (app);
  scrolled = GTK_WIDGET (gtk_builder_get_object (builder, "processes_scrolled"));

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), GTK_WIDGET (proctree));

  app->proc_actionbar_revealer = GTK_REVEALER (gtk_builder_get_object (builder, "proc_actionbar_revealer"));

  /* create popover_menu for the processes tab */
  GMenuModel *menu_model = G_MENU_MODEL (gtk_builder_get_object (builder, "process-popup-menu"));

  app->proc_popover_menu = GTK_POPOVER (gtk_popover_menu_new_from_model (menu_model));
  gtk_popover_set_has_arrow (app->proc_popover_menu, FALSE);
  gtk_popover_set_default_widget (app->proc_popover_menu, GTK_WIDGET (scrolled));
  gtk_widget_set_parent (GTK_WIDGET (app->proc_popover_menu), GTK_WIDGET (scrolled));

  app->end_process_button = GTK_BUTTON (gtk_builder_get_object (builder, "end_process_button"));
  app->search_button = GTK_TOGGLE_BUTTON (gtk_builder_get_object (builder, "search_button"));
  app->search_bar = GTK_SEARCH_BAR (gtk_builder_get_object (builder, "proc_searchbar"));
  app->search_entry = GTK_SEARCH_ENTRY (gtk_builder_get_object (builder, "proc_searchentry"));

  gtk_search_bar_set_key_capture_widget (app->search_bar, GTK_WIDGET (app->main_window));

  g_signal_connect (app->search_entry,
                    "changed",
                    G_CALLBACK (search_text_changed),
                    app);

  g_object_bind_property (app->search_bar, "search-mode-enabled", app->search_button, "active", (GBindingFlags)(G_BINDING_BIDIRECTIONAL));
}

static void
cb_cpu_color_changed (GsmColorButton *cp,
                      gpointer        data)
{
  guint cpu_i = GPOINTER_TO_UINT (data);
  auto settings = Gio::Settings::create (GSM_GSETTINGS_SCHEMA);

  /* Get current values */
  GVariant *cpu_colors_var = g_settings_get_value (settings->gobj (), GSM_SETTING_CPU_COLORS);
  gsize children_n = g_variant_n_children (cpu_colors_var);

  /* Create builder to construct new setting with updated value for cpu i */
  GVariantBuilder builder;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

  for (guint i = 0; i < children_n; i++)
    {
      if (cpu_i == i)
        {
          gchar *color;
          GdkRGBA button_color;
          gsm_color_button_get_color (cp, &button_color);
          color = gdk_rgba_to_string (&button_color);
          g_variant_builder_add (&builder, "(us)", i, color);
          g_free (color);
        }
      else
        {
          g_variant_builder_add_value (&builder,
                                       g_variant_get_child_value (cpu_colors_var, i));
        }
    }

  /* Just set the value and let the changed::cpu-colors signal callback do the rest. */
  settings->set_value (GSM_SETTING_CPU_COLORS, Glib::wrap (g_variant_builder_end (&builder)));
}

static void
change_settings_color (Gio::Settings&  settings,
                       const char     *key,
                       GsmColorButton *cp)
{
  GdkRGBA c;
  char *color;

  gsm_color_button_get_color (cp, &c);
  color = gdk_rgba_to_string (&c);
  settings.set_string (key, color);
  g_free (color);
}

static void
cb_mem_color_changed (GsmColorButton *cp,
                      gpointer        data)
{
  GsmApplication *app = (GsmApplication *) data;

  change_settings_color (*app->settings.operator-> (), GSM_SETTING_MEM_COLOR, cp);
}


static void
cb_swap_color_changed (GsmColorButton *cp,
                       gpointer        data)
{
  GsmApplication *app = (GsmApplication *) data;

  change_settings_color (*app->settings.operator-> (), GSM_SETTING_SWAP_COLOR, cp);
}

static void
cb_net_in_color_changed (GsmColorButton *cp,
                         gpointer        data)
{
  GsmApplication *app = (GsmApplication *) data;

  change_settings_color (*app->settings.operator-> (), GSM_SETTING_NET_IN_COLOR, cp);
}

static void
cb_net_out_color_changed (GsmColorButton *cp,
                          gpointer        data)
{
  GsmApplication *app = (GsmApplication *) data;

  change_settings_color (*app->settings.operator-> (), GSM_SETTING_NET_OUT_COLOR, cp);
}

static void
cb_disk_read_color_changed (GsmColorButton *cp,
                            gpointer        data)
{
  GsmApplication *app = (GsmApplication *) data;

  change_settings_color (*app->settings.operator-> (), GSM_SETTING_DISK_READ_COLOR, cp);
}

static void
cb_disk_write_color_changed (GsmColorButton *cp,
                             gpointer        data)
{
  GsmApplication *app = (GsmApplication *) data;

  change_settings_color (*app->settings.operator-> (), GSM_SETTING_DISK_WRITE_COLOR, cp);
}

static void
create_sys_view (GsmApplication *app,
                 GtkBuilder     *builder)
{
  GtkBox *cpu_graph_box, *mem_graph_box, *net_graph_box, *disk_graph_box;
  GtkExpander *cpu_expander, *mem_expander, *net_expander, *disk_expander;
  GtkLabel *label, *cpu_label;
  GtkGrid *table;
  GsmColorButton *color_picker;
  GtkCssProvider *provider;

  LoadGraph *cpu_graph, *mem_graph, *net_graph, *disk_graph;

  gint i;
  gchar *title_text;
  gchar *label_text;
  gchar *title_template;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (provider, LOAD_GRAPH_CSS);
  gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  // Translators: color picker title, %s is CPU, Memory, Swap, Receiving, Sending
  title_template = g_strdup (_("Pick a Color for “%s”"));

  /* The CPU BOX */

  cpu_graph_box = GTK_BOX (gtk_builder_get_object (builder, "cpu_graph_box"));
  cpu_expander = GTK_EXPANDER (gtk_builder_get_object (builder, "cpu_expander"));
  g_object_bind_property (cpu_expander, "expanded", cpu_expander, "vexpand", G_BINDING_DEFAULT);

  cpu_graph = new LoadGraph (LOAD_GRAPH_CPU);
  gtk_widget_set_size_request (GTK_WIDGET (load_graph_get_widget (cpu_graph)), -1, 70);
  gtk_box_prepend (cpu_graph_box,
                   GTK_WIDGET (load_graph_get_widget (cpu_graph)));

  GtkGrid*cpu_table = GTK_GRID (gtk_builder_get_object (builder, "cpu_table"));
  gint cols = 4;
  gint rows = (app->config.num_cpus + cols - 1) / cols;

  for (i = 0; i < app->config.num_cpus; i++)
    {
      GtkBox *temp_hbox;

      temp_hbox = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));
      gtk_box_set_spacing (temp_hbox, 4);
      if (i < cols)
        gtk_grid_insert_column (cpu_table, i % cols);
      if ((i + 1) % cols == cols)
        gtk_grid_insert_row (cpu_table, (i + 1) / cols);
      gtk_grid_attach (cpu_table, GTK_WIDGET (temp_hbox), i / rows, i % rows, 1, 1);

      color_picker = gsm_color_button_new (&cpu_graph->colors.at (i), GSMCP_TYPE_CPU);
      g_signal_connect (G_OBJECT (color_picker), "color-set",
                        G_CALLBACK (cb_cpu_color_changed), GINT_TO_POINTER (i));
      gtk_box_append (temp_hbox, GTK_WIDGET (color_picker));
      gtk_widget_set_size_request (GTK_WIDGET (color_picker), 32, -1);

      if (app->config.num_cpus == 1)
        label_text = g_strdup (_("CPU"));
      else
        label_text = g_strdup_printf (_("CPU%d"), i + 1);
      title_text = g_strdup_printf (title_template, label_text);
      label = GTK_LABEL (gtk_label_new (label_text));
      if (app->config.num_cpus >= 10)
        gtk_label_set_width_chars (label, log10 (app->config.num_cpus) + 1 + 4);
      gsm_color_button_set_title (color_picker, title_text);
      g_free (title_text);
      gtk_box_append (temp_hbox, GTK_WIDGET (label));
      g_free (label_text);

      cpu_label = make_tnum_label ();

      /* Reserve some space to avoid the layout changing with the values. */
      gtk_label_set_width_chars (cpu_label, 6);
      gtk_box_append (temp_hbox, GTK_WIDGET (cpu_label));
      load_graph_get_labels (cpu_graph)->cpu[i] = cpu_label;
    }

  app->cpu_graph = cpu_graph;

  /** The memory box */

  mem_graph_box = GTK_BOX (gtk_builder_get_object (builder, "mem_graph_box"));
  mem_expander = GTK_EXPANDER (gtk_builder_get_object (builder, "mem_expander"));
  g_object_bind_property (mem_expander, "expanded", mem_expander, "vexpand", G_BINDING_DEFAULT);

  mem_graph = new LoadGraph (LOAD_GRAPH_MEM);
  gtk_widget_set_size_request (GTK_WIDGET (load_graph_get_widget (mem_graph)), -1, 70);
  gtk_box_prepend (mem_graph_box,
                   GTK_WIDGET (load_graph_get_widget (mem_graph)));

  table = GTK_GRID (gtk_builder_get_object (builder, "mem_table"));

  color_picker = load_graph_get_mem_color_picker (mem_graph);
  g_signal_connect (G_OBJECT (color_picker), "color-set",
                    G_CALLBACK (cb_mem_color_changed), app);
  title_text = g_strdup_printf (title_template, _("Memory"));
  gsm_color_button_set_title (color_picker, title_text);
  g_free (title_text);

  label = GTK_LABEL (gtk_builder_get_object (builder, "memory_label"));

  gtk_grid_attach_next_to (table, GTK_WIDGET (color_picker), GTK_WIDGET (label), GTK_POS_LEFT, 1, 3);
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (mem_graph)->memory), GTK_WIDGET (label), GTK_POS_BOTTOM, 1, 2);

  color_picker = load_graph_get_swap_color_picker (mem_graph);
  g_signal_connect (G_OBJECT (color_picker), "color-set",
                    G_CALLBACK (cb_swap_color_changed), app);
  title_text = g_strdup_printf (title_template, _("Swap"));
  gsm_color_button_set_title (GSM_COLOR_BUTTON (color_picker), title_text);
  g_free (title_text);

  label = GTK_LABEL (gtk_builder_get_object (builder, "swap_label"));

  gtk_grid_attach_next_to (table, GTK_WIDGET (color_picker), GTK_WIDGET (label), GTK_POS_LEFT, 1, 3);
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (mem_graph)->swap), GTK_WIDGET (label), GTK_POS_BOTTOM, 1, 2);

  app->mem_graph = mem_graph;

  /* The net box */

  net_graph_box = GTK_BOX (gtk_builder_get_object (builder, "net_graph_box"));
  net_expander = GTK_EXPANDER (gtk_builder_get_object (builder, "net_expander"));
  g_object_bind_property (net_expander, "expanded", net_expander, "vexpand", G_BINDING_DEFAULT);

  net_graph = new LoadGraph (LOAD_GRAPH_NET);
  gtk_widget_set_size_request (GTK_WIDGET (load_graph_get_widget (net_graph)), -1, 70);
  gtk_box_prepend (net_graph_box,
                   GTK_WIDGET (load_graph_get_widget (net_graph)));

  table = GTK_GRID (gtk_builder_get_object (builder, "net_table"));

  color_picker = gsm_color_button_new (
    &net_graph->colors.at (0), GSMCP_TYPE_NETWORK_IN);
  gtk_widget_set_valign (GTK_WIDGET (color_picker), GTK_ALIGN_CENTER);
  g_signal_connect (G_OBJECT (color_picker), "color-set",
                    G_CALLBACK (cb_net_in_color_changed), app);
  title_text = g_strdup_printf (title_template, _("Receiving"));
  gsm_color_button_set_title (color_picker, title_text);
  g_free (title_text);

  label = GTK_LABEL (gtk_builder_get_object (builder, "receiving_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (color_picker), GTK_WIDGET (label), GTK_POS_LEFT, 1, 2);
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (net_graph)->net_in), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);
  label = GTK_LABEL (gtk_builder_get_object (builder, "total_received_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (net_graph)->net_in_total), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);

  color_picker = gsm_color_button_new (
    &net_graph->colors.at (1), GSMCP_TYPE_NETWORK_OUT);
  gtk_widget_set_valign (GTK_WIDGET (color_picker), GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (GTK_WIDGET (color_picker), true);
  gtk_widget_set_halign (GTK_WIDGET (color_picker), GTK_ALIGN_END);

  g_signal_connect (G_OBJECT (color_picker), "color-set",
                    G_CALLBACK (cb_net_out_color_changed), app);
  title_text = g_strdup_printf (title_template, _("Sending"));
  gsm_color_button_set_title (color_picker, title_text);
  g_free (title_text);

  label = GTK_LABEL (gtk_builder_get_object (builder, "sending_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (color_picker), GTK_WIDGET (label), GTK_POS_LEFT, 1, 2);
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (net_graph)->net_out), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);
  label = GTK_LABEL (gtk_builder_get_object (builder, "total_sent_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (net_graph)->net_out_total), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);
  gtk_widget_set_hexpand (GTK_WIDGET (load_graph_get_labels (net_graph)->net_out_total), true);
  gtk_widget_set_halign (GTK_WIDGET (load_graph_get_labels (net_graph)->net_out_total), GTK_ALIGN_START);

  gtk_widget_set_hexpand (GTK_WIDGET (load_graph_get_labels (net_graph)->net_out), true);
  gtk_widget_set_halign (GTK_WIDGET (load_graph_get_labels (net_graph)->net_out), GTK_ALIGN_START);

  app->net_graph = net_graph;

  /* The disk box */

  disk_graph_box = GTK_BOX (gtk_builder_get_object (builder, "disk_graph_box"));
  disk_expander = GTK_EXPANDER (gtk_builder_get_object (builder, "disk_expander"));
  g_object_bind_property (disk_expander, "expanded", disk_expander, "vexpand", G_BINDING_DEFAULT);

  disk_graph = new LoadGraph (LOAD_GRAPH_DISK);
  gtk_widget_set_size_request (GTK_WIDGET (load_graph_get_widget (disk_graph)), -1, 70);
  gtk_box_prepend (disk_graph_box,
                   GTK_WIDGET (load_graph_get_widget (disk_graph)));

  table = GTK_GRID (gtk_builder_get_object (builder, "disk_table"));

  color_picker = gsm_color_button_new (
    &disk_graph->colors.at (0), GSMCP_TYPE_DISK_READ);
  gtk_widget_set_valign (GTK_WIDGET (color_picker), GTK_ALIGN_CENTER);
  g_signal_connect (G_OBJECT (color_picker), "color-set",
                    G_CALLBACK (cb_disk_read_color_changed), app);
  title_text = g_strdup_printf (title_template, _("Reading"));
  gsm_color_button_set_title (color_picker, title_text);
  g_free (title_text);

  label = GTK_LABEL (gtk_builder_get_object (builder, "reading_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (color_picker), GTK_WIDGET (label), GTK_POS_LEFT, 1, 2);
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_read), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);
  label = GTK_LABEL (gtk_builder_get_object (builder, "total_read_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_read_total), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);

  color_picker = gsm_color_button_new (
    &disk_graph->colors.at (1), GSMCP_TYPE_DISK_WRITE);
  gtk_widget_set_valign (GTK_WIDGET (color_picker), GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (GTK_WIDGET (color_picker), true);
  gtk_widget_set_halign (GTK_WIDGET (color_picker), GTK_ALIGN_END);

  g_signal_connect (G_OBJECT (color_picker), "color-set",
                    G_CALLBACK (cb_disk_write_color_changed), app);
  title_text = g_strdup_printf (title_template, _("Writing"));
  gsm_color_button_set_title (color_picker, title_text);
  g_free (title_text);

  label = GTK_LABEL (gtk_builder_get_object (builder, "writing_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (color_picker), GTK_WIDGET (label), GTK_POS_LEFT, 1, 2);
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_write), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);
  label = GTK_LABEL (gtk_builder_get_object (builder, "total_written_label"));
  gtk_grid_attach_next_to (table, GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_write_total), GTK_WIDGET (label), GTK_POS_RIGHT, 1, 1);
  gtk_widget_set_hexpand (GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_write_total), true);
  gtk_widget_set_halign (GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_write_total), GTK_ALIGN_START);

  gtk_widget_set_hexpand (GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_write), true);
  gtk_widget_set_halign (GTK_WIDGET (load_graph_get_labels (disk_graph)->disk_write), GTK_ALIGN_START);

  app->disk_graph = disk_graph;
  g_free (title_template);
}

static void
on_activate_about (GSimpleAction *,
                   GVariant *,
                   gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  const gchar * const authors[] = {
    "Kevin Vandersloot",
    "Erik Johnsson",
    "Jorgen Scheibengruber",
    "Benoît Dejean",
    "Paolo Borelli",
    "Karl Lattimer",
    "Chris Kühl",
    "Robert Roth",
    "Stefano Facchini",
    "Jacob Barkdull",
    "Ondřej Míchal",
    NULL
  };

  const gchar * const documenters[] = {
    "Bill Day",
    "Sun Microsystems",
    NULL
  };

  const gchar * const artists[] = {
    "Baptiste Mille-Mathias",
    NULL
  };

  adw_show_about_window (
    GTK_WINDOW (app->main_window),
    "application-name", _("Lingmo OS System Monitor"),
    "comments", _("Developed based on GNOME System Monitor "
                  "View current processes and monitor "
                  "system state"),
    "version", VERSION,
    "website", "https://lingmo.org",
    "copyright", "Copyright \xc2\xa9 2001-2004 Kevin Vandersloot\n"
    "Copyright \xc2\xa9 2005-2007 Benoît Dejean\n"
    "Copyright \xc2\xa9 2011 Chris Kühl\n",
    "Copyright \xc2\xa9 2024 Lingmo OS Team"
    "application-icon", APP_ID,
    "developer-name", _("Lingmo OS"),
    "developers", authors,
    "artists", artists,
    "documenters", documenters,
    "translator-credits", _("translator-credits"),
    "license-type", GTK_LICENSE_GPL_2_0,
    "website", "https://wiki.gnome.org/Apps/SystemMonitor",
    "support-url", "https://help.gnome.org/users/gnome-system-monitor/stable/",
    "issue-url", "https://gitlab.gnome.org/GNOME/gnome-system-monitor/issues",
    NULL);
}

static void
on_activate_keyboard_shortcuts (GSimpleAction *,
                                GVariant *,
                                gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  gtk_widget_set_visible (GTK_WIDGET (gtk_application_window_get_help_overlay (GTK_APPLICATION_WINDOW (app->main_window))), TRUE);
}

static void
on_activate_refresh (GSimpleAction *,
                     GVariant *,
                     gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  proctable_update (app);
}

static void
kill_process_with_confirmation (GsmApplication *app,
                                int             signal)
{
  gboolean kill_dialog = app->settings->get_boolean (GSM_SETTING_SHOW_KILL_DIALOG);

  if (kill_dialog)
    procdialog_create_kill_dialog (app, signal);
  else
    kill_process (app, signal);
}

static void
on_activate_send_signal (GSimpleAction *action,
                         GVariant *,
                         gpointer       data)
{
  GsmApplication *app = (GsmApplication *) data;

  const gchar *action_name = g_action_get_name (G_ACTION (action));

  if (g_strcmp0 (action_name, "send-signal-cont") == 0)
    kill_process (app, SIGCONT);
  else if (g_strcmp0 (action_name, "send-signal-stop") == 0)
    kill_process_with_confirmation (app, SIGSTOP);
  else if (g_strcmp0 (action_name, "send-signal-term") == 0)
    kill_process_with_confirmation (app, SIGTERM);
  else if (g_strcmp0 (action_name, "send-signal-kill") == 0)
    kill_process_with_confirmation (app, SIGKILL);
}

static void
on_activate_set_affinity (GSimpleAction *,
                          GVariant *,
                          gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  create_set_affinity_dialog (app);
}

static void
on_activate_memory_maps (GSimpleAction *,
                         GVariant *,
                         gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  create_memmaps_dialog (app);
}

static void
on_activate_open_files (GSimpleAction *,
                        GVariant *,
                        gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  create_openfiles_dialog (app);
}

static void
on_activate_process_properties (GSimpleAction *,
                                GVariant *,
                                gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  create_procproperties_dialog (app);
}

static void
on_activate_radio (GSimpleAction *action,
                   GVariant      *parameter,
                   gpointer)
{
  g_action_change_state (G_ACTION (action), parameter);
}

static void
on_activate_toggle (GSimpleAction *action,
                    GVariant*,
                    gpointer)
{
  GVariant *state = g_action_get_state (G_ACTION (action));

  g_action_change_state (G_ACTION (action), g_variant_new_boolean (!g_variant_get_boolean (state)));
  g_variant_unref (state);
}

static void
on_activate_search (GSimpleAction*,
                    GVariant*,
                    gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  gtk_search_bar_set_search_mode (app->search_bar, !gtk_search_bar_get_search_mode (app->search_bar));
}

static void
change_show_page_state (GSimpleAction *action,
                        GVariant      *state,
                        gpointer       data)
{
  GsmApplication *app = (GsmApplication *) data;

  g_simple_action_set_state (action, state);
  auto tab = g_variant_get_string (state, NULL);
  app->settings->set_string (GSM_SETTING_CURRENT_TAB, tab);
  app->config.current_tab = tab;
  adw_view_stack_set_visible_child_name (app->stack, tab);
}

static void
change_show_processes_state (GSimpleAction *action,
                             GVariant      *state,
                             gpointer       data)
{
  GsmApplication *app = (GsmApplication *) data;

  auto state_var = Glib::wrap (state, true);

  g_simple_action_set_state (action, state);
  app->settings->set_value (GSM_SETTING_SHOW_WHOSE_PROCESSES, state_var);
}

static void
change_show_dependencies_state (GSimpleAction *action,
                                GVariant      *state,
                                gpointer       data)
{
  GsmApplication *app = (GsmApplication *) data;

  auto state_var = Glib::wrap (state, true);

  g_simple_action_set_state (action, state);
  app->settings->set_value (GSM_SETTING_SHOW_DEPENDENCIES, state_var);
}

static void
on_activate_priority (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       data)
{
  GsmApplication *app = (GsmApplication *) data;

  g_action_change_state (G_ACTION (action), parameter);

  const gint32 priority = g_variant_get_int32 (parameter);

  switch (priority)
    {
      case 32:
        procdialog_create_renice_dialog (app);
        break;

      default:
        renice (app, priority);
        break;
    }
}

static void
change_priority_state (GSimpleAction *action,
                       GVariant      *state,
                       gpointer)
{
  g_simple_action_set_state (action, state);
}

static void
update_page_activities (GsmApplication *app)
{
  const gchar *current_page = adw_view_stack_get_visible_child_name (app->stack);

  if (app->config.current_tab != current_page)
    {
      app->settings->set_string (GSM_SETTING_CURRENT_TAB, current_page);
      app->config.current_tab = current_page;
    }

  if (strcmp (current_page, "processes") == 0)
    {
      proctable_update (app);
      proctable_thaw (app);

      gtk_widget_set_visible (GTK_WIDGET (app->end_process_button), TRUE);
      gtk_widget_set_visible (GTK_WIDGET (app->search_button), TRUE);

      gtk_menu_button_set_menu_model (app->app_menu_button, app->process_window_menu_model);
      gtk_search_bar_set_key_capture_widget (app->search_bar, GTK_WIDGET (app->main_window));

      update_sensitivity (app);

      if (gtk_search_bar_get_search_mode (app->search_bar))
        gtk_widget_grab_focus (GTK_WIDGET (app->search_entry));
      else
        gtk_widget_grab_focus (GTK_WIDGET (app->tree));
    }
  else
    {
      proctable_freeze (app);

      gtk_widget_set_visible (GTK_WIDGET (app->end_process_button), FALSE);
      gtk_widget_set_visible (GTK_WIDGET (app->search_button), FALSE);

      gtk_menu_button_set_menu_model (app->app_menu_button, app->generic_window_menu_model);
      gtk_search_bar_set_key_capture_widget (app->search_bar, NULL);

      update_sensitivity (app);
    }

  if (strcmp (current_page, "resources") == 0)
    {
      load_graph_start (app->cpu_graph);
      load_graph_start (app->mem_graph);
      load_graph_start (app->net_graph);
      load_graph_start (app->disk_graph);
    }
  else
    {
      load_graph_stop (app->cpu_graph);
      load_graph_stop (app->mem_graph);
      load_graph_stop (app->net_graph);
      load_graph_stop (app->disk_graph);
    }

  if (strcmp (current_page, "disks") == 0)
    {
      disks_update (app);
      disks_thaw (app);
    }
  else
    {
      disks_freeze (app);
    }
}

static void
cb_change_current_page (AdwViewStack*,
                        GParamSpec*,
                        gpointer data)
{
  update_page_activities ((GsmApplication *)data);
}

static gboolean
cb_main_window_delete (GtkWindow*,
                       gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;

  app->shutdown ();

  return TRUE;
}

static void
cb_main_window_suspended (GtkWindow*,
                          gboolean        suspended,
                          GsmApplication *app)
{
  auto current_page = app->config.current_tab;

  if (suspended)
    {
      if (current_page == "processes")
        {
          proctable_freeze (app);
        }
      else if (current_page == "resources")
        {
          load_graph_stop (app->cpu_graph);
          load_graph_stop (app->mem_graph);
          load_graph_stop (app->net_graph);
          load_graph_stop (app->disk_graph);
        }
      else if (current_page == "disks")
        {
          disks_freeze (app);
        }
    }
  else
    {
      if (current_page == "processes")
        {
          proctable_update (app);
          proctable_thaw (app);
        }
      else if (current_page == "resources")
        {
          load_graph_start (app->cpu_graph);
          load_graph_start (app->mem_graph);
          load_graph_start (app->net_graph);
          load_graph_start (app->disk_graph);
        }
      else if (current_page == "disks")
        {
          disks_update (app);
          disks_thaw (app);
        }
    }
}

void
create_main_window (GsmApplication *app)
{
  GdkDisplay *display;
  GdkMonitor *monitor;
  GdkRectangle monitor_geometry;
  GdkSurface *surface;
  GtkBuilder *builder = gtk_builder_new ();
  GtkNative *native;
  GError *err = NULL;
  int width, height;

  gtk_builder_add_from_resource (builder, "/org/lingmo/lingmo-system-monitor/data/interface.ui", &err);
  if (err != NULL)
    g_error ("%s", err->message);
  gtk_builder_add_from_resource (builder, "/org/lingmo/lingmo-system-monitor/data/menus.ui", &err);
  if (err != NULL)
    g_error ("%s", err->message);
  gtk_builder_add_from_resource (builder, "/org/lingmo/lingmo-system-monitor/gtk/help-overlay.ui", &err);
  if (err != NULL)
    g_error ("%s", err->message);

  app->main_window = ADW_APPLICATION_WINDOW (gtk_builder_get_object (builder, "main_window"));
  gtk_window_set_application (GTK_WINDOW (app->main_window), app->gobj ());
  gtk_widget_set_name (GTK_WIDGET (app->main_window), "lingmo-system-monitor");

  gtk_application_window_set_help_overlay (GTK_APPLICATION_WINDOW (app->main_window),
                                           GTK_SHORTCUTS_WINDOW (gtk_builder_get_object (builder, "help_overlay")));

  /* create the main stack */
  app->stack = ADW_VIEW_STACK (gtk_builder_get_object (builder, "stack"));
  create_proc_view (app, builder);
  create_sys_view (app, builder);
  create_disk_view (app, builder);

  app->app_menu_button = GTK_MENU_BUTTON (gtk_builder_get_object (builder, "app_menu_button"));
  app->generic_window_menu_model = G_MENU_MODEL (gtk_builder_get_object (builder, "generic-window-menu"));
  app->process_window_menu_model = G_MENU_MODEL (gtk_builder_get_object (builder, "process-window-menu"));

  // GtkMenuButton owns its models and thus an additional reference needs to be held
  g_object_ref (G_OBJECT (app->generic_window_menu_model));
  g_object_ref (G_OBJECT (app->process_window_menu_model));

  gtk_menu_button_set_menu_model (app->app_menu_button, app->generic_window_menu_model);

  GActionEntry win_action_entries[] = {
    { "about", on_activate_about, NULL, NULL, NULL, { 0, 0, 0 } },
    { "show-help-overlay", on_activate_keyboard_shortcuts, NULL, NULL, NULL, { 0, 0, 0 } },
    { "search", on_activate_search, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-stop", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-cont", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-term", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "send-signal-kill", on_activate_send_signal, NULL, NULL, NULL, { 0, 0, 0 } },
    { "priority", on_activate_priority, "i", "@i 0", change_priority_state, { 0, 0, 0 } },
    { "set-affinity", on_activate_set_affinity, NULL, NULL, NULL, { 0, 0, 0 } },
    { "memory-maps", on_activate_memory_maps, NULL, NULL, NULL, { 0, 0, 0 } },
    { "open-files", on_activate_open_files, NULL, NULL, NULL, { 0, 0, 0 } },
    { "process-properties", on_activate_process_properties, NULL, NULL, NULL, { 0, 0, 0 } },
    { "refresh", on_activate_refresh, NULL, NULL, NULL, { 0, 0, 0 } },
    { "show-page", on_activate_radio, "s", "'resources'", change_show_page_state, { 0, 0, 0 } },
    { "show-whose-processes", on_activate_radio, "s", "'all'", change_show_processes_state, { 0, 0, 0 } },
    { "show-dependencies", on_activate_toggle, NULL, "false", change_show_dependencies_state, { 0, 0, 0 } }
  };
  g_action_map_add_action_entries (G_ACTION_MAP (app->main_window),
                                   win_action_entries,
                                   G_N_ELEMENTS (win_action_entries),
                                   app);

  adw_view_stack_set_visible_child_name (ADW_VIEW_STACK (app->stack), app->config.current_tab.c_str ());

  g_signal_connect (G_OBJECT (app->stack),
                    "notify::visible-child",
                    G_CALLBACK (cb_change_current_page),
                    app);
  g_signal_connect (G_OBJECT (app->main_window),
                    "notify::suspended",
                    G_CALLBACK (cb_main_window_suspended),
                    app);
  g_signal_connect (G_OBJECT (app->main_window),
                    "destroy",
                    G_CALLBACK (cb_main_window_delete),
                    app);

  GAction *action;

  action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window),
                                       "show-dependencies");
  g_action_change_state (action,
                         g_settings_get_value (app->settings->gobj (), GSM_SETTING_SHOW_DEPENDENCIES));


  action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window),
                                       "show-whose-processes");
  g_action_change_state (action,
                         g_settings_get_value (app->settings->gobj (), GSM_SETTING_SHOW_WHOSE_PROCESSES));

  // Surface is available only after a widget has been shown
  gtk_widget_set_visible (GTK_WIDGET (app->main_window), true);

  g_settings_bind (app->settings->gobj (), GSM_SETTING_WINDOW_WIDTH,
                   app->main_window, "default-width",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (app->settings->gobj (), GSM_SETTING_WINDOW_HEIGHT,
                   app->main_window, "default-height",
                   G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (app->settings->gobj (), GSM_SETTING_MAXIMIZED,
                   app->main_window, "maximized",
                   G_SETTINGS_BIND_DEFAULT);

  update_page_activities (app);

  g_object_unref (G_OBJECT (builder));
}

static gboolean
scroll_to_selection (gpointer data)
{
  GsmApplication *app = (GsmApplication *) data;
  GList*paths = gtk_tree_selection_get_selected_rows (app->selection, NULL);
  guint length = g_list_length (paths);

  if (length > 0)
    {
      GtkTreePath*last_path = (GtkTreePath*) g_list_nth_data (paths, length - 1);
      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (app->tree), last_path, NULL, FALSE, 0.0, 0.0);
    }

  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
  return FALSE;
}

void
update_sensitivity (GsmApplication *app)
{
  const char * const selected_actions[] = { "send-signal-stop",
                                            "send-signal-cont",
                                            "send-signal-term",
                                            "send-signal-kill",
                                            "priority",
                                            "set-affinity",
                                            "memory-maps",
                                            "open-files",
                                            "process-properties" };

  const char * const processes_actions[] = { "refresh",
                                             "search",
                                             "show-whose-processes",
                                             "show-dependencies" };

  size_t i;
  gboolean processes_sensitivity, selected_sensitivity;
  GAction *action;

  processes_sensitivity = (strcmp (adw_view_stack_get_visible_child_name (app->stack), "processes") == 0);
  selected_sensitivity = gtk_tree_selection_count_selected_rows (app->selection) > 0;

  for (i = 0; i != G_N_ELEMENTS (processes_actions); ++i)
    {
      action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window),
                                           processes_actions[i]);
      g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                                   processes_sensitivity);
    }

  for (i = 0; i != G_N_ELEMENTS (selected_actions); ++i)
    {
      action = g_action_map_lookup_action (G_ACTION_MAP (app->main_window),
                                           selected_actions[i]);
      g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                                   processes_sensitivity & selected_sensitivity);
    }

  gtk_revealer_set_reveal_child (GTK_REVEALER (app->proc_actionbar_revealer),
                                 selected_sensitivity);

  // Scrolls the table to selected row. Useful when the last row is obstructed by the revealer
  guint duration_ms = gtk_revealer_get_transition_duration (GTK_REVEALER (app->proc_actionbar_revealer));

  g_timeout_add (duration_ms, scroll_to_selection, app);
}
