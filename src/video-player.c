#include <skeltrack.h>
#include <math.h>
#include <string.h>
#include <cairo.h>
#include <glib-object.h>
#include <clutter/clutter.h>
#include <clutter/clutter-keysyms.h>

static SkeltrackSkeleton *skeleton = NULL;
static ClutterActor *info_text;
static ClutterActor *skeleton_tex;
static ClutterActor *depth_tex;
static ClutterActor *instructions;

static GList *skeleton_list = NULL;
static GList *frame_path_list = NULL;
static GList *frame_list = NULL;

static GList *current_frame_path = NULL;
static GList *current_frame = NULL;
static GList *current_skeleton = NULL;

static gboolean SHOW_SKELETON = TRUE;
static gboolean ENABLE_SMOOTHING = FALSE;
static gfloat SMOOTHING_FACTOR = .0;

static guint THRESHOLD_BEGIN = 500;
/* Adjust this value to increase of decrease
   the threshold */
static guint THRESHOLD_END   = 8000;

#define POINT_SIZE 6

static gint width = 640;
static gint height = 480;
static gint dimension_reduction = 16;

static guint current_frame_number = 0;

typedef struct
{
  guint16 *reduced_buffer;
  gint width;
  gint height;
  gint reduced_width;
  gint reduced_height;
} BufferInfo;

static void
set_orientation ()
{
  ClutterActor *stage;

  stage = clutter_stage_get_default ();

  clutter_actor_set_size (skeleton_tex, width, height);
  clutter_actor_set_size (depth_tex, width, height);
  clutter_cairo_texture_set_surface_size (CLUTTER_CAIRO_TEXTURE (skeleton_tex), width, height);
  clutter_cairo_texture_set_surface_size (CLUTTER_CAIRO_TEXTURE (depth_tex), width, height);
  clutter_actor_set_size (stage, width * 2, height + 250);
  clutter_actor_set_position (depth_tex, width, 0.0);
  clutter_actor_set_position (info_text, 50, height + 20);
  clutter_actor_set_position (instructions, 50, height + 70);
}


static void
grayscale_buffer_set_value (guchar *buffer, gint index, guchar value)
{
  buffer[index * 3] = value;
  buffer[index * 3 + 1] = value;
  buffer[index * 3 + 2] = value;
}

static void
draw_point (guchar *buffer,
            guint width,
            guint height,
            gchar *color_str,
            guint x,
            guint y)
{
  ClutterColor *color = clutter_color_new (0, 0, 0, 255);
  clutter_color_from_string (color, color_str);
  gint i, j;
  for (i = -POINT_SIZE; i < POINT_SIZE; i++)
    {
      for (j = -POINT_SIZE; j < POINT_SIZE; j++)
        {
          if (x + i < 0 || x + i >= width ||
              y + j < 0 || y + j >= height)
            continue;

          buffer[(width * (y + j) + x + i) * 3] = color->red;
          buffer[(width * (y + j) + x + i) * 3 + 1] = color->green;
          buffer[(width * (y + j) + x + i) * 3 + 2] = color->blue;
        }
    }

  clutter_color_free (color);
}

static BufferInfo *
process_buffer (guint16 *buffer,
                guint width,
                guint height,
                guint dimension_factor,
                guint threshold_begin,
                guint threshold_end)
{
  BufferInfo *buffer_info;
  gint i, j, reduced_width, reduced_height;
  guint16 *reduced_buffer;

  g_return_val_if_fail (buffer != NULL, NULL);

  reduced_width = (width - width % dimension_factor) / dimension_factor;
  reduced_height = (height - height % dimension_factor) / dimension_factor;

  reduced_buffer = g_slice_alloc0 (reduced_width * reduced_height *
                                   sizeof (guint16));

  for (i = 0; i < reduced_width; i++)
    {
      for (j = 0; j < reduced_height; j++)
        {
          gint index;
          guint16 value;

          index = j * width * dimension_factor + i * dimension_factor;
          value = buffer[index];

          if (value < threshold_begin || value > threshold_end)
            {
              reduced_buffer[j * reduced_width + i] = 0;
              continue;
            }

          reduced_buffer[j * reduced_width + i] = value;
        }
    }

  buffer_info = g_slice_new0 (BufferInfo);
  buffer_info->reduced_buffer = reduced_buffer;
  buffer_info->reduced_width = reduced_width;
  buffer_info->reduced_height = reduced_height;
  buffer_info->width = width;
  buffer_info->height = height;

  return buffer_info;
}

static guchar *
create_grayscale_buffer (guint16 *buffer, gint width, gint height)
{
  gint i,j;
  gint size;
  guchar *grayscale_buffer;

  size = width * height * sizeof (guchar) * 3;
  grayscale_buffer = g_slice_alloc (size);
  /*Paint is white*/
  memset (grayscale_buffer, 255, size);

  for (i = 0; i < width; i++)
    {
      for (j = 0; j < height; j++)
        {
          guint16 value = round (buffer[j * width + i] * 256. / 3000.);
          if (value != 0)
            {
              gint index = j * width + i;
              grayscale_buffer_set_value (grayscale_buffer, index, value);
            }
        }
    }

  return grayscale_buffer;
}

static GList *
get_frame_path_list (const gchar *directory)
{
  GDir *dir = NULL;
  GError *error = NULL;
  GList *list = NULL;

  dir = g_dir_open (directory, 0, &error);

  if (error)
    {
      g_debug ("ERROR: %s", error->message);
      g_clear_error (&error);
      return NULL;
    }

  const gchar *current_file;
  while ((current_file = g_dir_read_name(dir)) != NULL)
    {
      gchar *current_path = g_strconcat (directory, "/", current_file, NULL);
      list = g_list_insert_sorted (list, current_path, (GCompareFunc) g_strcmp0);
    }

  return list;
}

static guint16 *
read_file_to_buffer (const gchar *name, gsize count, GError *e)
{
  GError *error = NULL;
  guint16 *depth = NULL;
  GFile *new_file = g_file_new_for_path (name);
  GFileInputStream *input_stream = g_file_read (new_file,
                                                NULL,
                                                &error);
  if (error != NULL)
    {
      g_debug ("ERROR: %s", error->message);
    }
  else
    {
      gsize bread = 0;
      depth = g_slice_alloc (count);
      g_input_stream_read_all ((GInputStream *) input_stream,
                               depth,
                               count,
                               &bread,
                               NULL,
                               &error);

      if (error != NULL)
        {
          g_debug ("ERROR: %s", error->message);
        }
    }
  return depth;
}

void read_video (gchar *directory,
                 gint width,
                 gint height,
                 gint dimension_reduction)
{
  GError *error = NULL;

  gchar *frame_path;
  gint i;

  frame_path_list = get_frame_path_list (directory);

  for (current_frame_path = g_list_first (frame_path_list);
       current_frame_path != NULL;
       current_frame_path = g_list_next (current_frame_path))
    {
      frame_path = (gchar *) current_frame_path->data;

      guint16 *depth = read_file_to_buffer (frame_path, width * height * sizeof
          (guint16), error);

      if (error != NULL)
        {
          g_debug ("ERROR: %s", error->message);
          return;
        }

      frame_list = g_list_append (frame_list, depth);
    }

  current_frame_path = g_list_first (frame_path_list);
}

static void
paint_joint (cairo_t *cairo,
             SkeltrackJoint *joint,
             gint radius,
             const gchar *color_str)
{
  ClutterColor *color;

  if (joint == NULL)
    return;

  color = clutter_color_new (0, 0, 0, 200);
  clutter_color_from_string (color, color_str);

  cairo_set_line_width (cairo, 10);
  clutter_cairo_set_source_color (cairo, color);
  cairo_arc (cairo,
             joint->screen_x,
             joint->screen_y,
             radius / joint->z,
             0,
             G_PI * 2);
  cairo_fill (cairo);
  clutter_color_free (color);
}

static void
connect_joints (cairo_t *cairo,
                SkeltrackJoint *joint_a,
                SkeltrackJoint *joint_b,
                const gchar *color_str)
{
  ClutterColor *color;

  if (joint_a == NULL || joint_b == NULL)
    return;

  color = clutter_color_new (0, 0, 0, 200);
  clutter_color_from_string (color, color_str);

  cairo_set_line_width (cairo, 10);
  clutter_cairo_set_source_color (cairo, color);
  cairo_move_to (cairo,
                 joint_a->screen_x,
                 joint_a->screen_y);
  cairo_line_to (cairo,
                 joint_b->screen_x,
                 joint_b->screen_y);
  cairo_stroke (cairo);
  clutter_color_free (color);
}

static void
on_texture_draw (ClutterCairoTexture *texture,
                 cairo_t *cairo,
                 gpointer user_data)
{
  guint width, height;
  ClutterColor *color;
  SkeltrackJoint *head, *left_hand, *right_hand, *shoulder_center,
    *left_shoulder, *right_shoulder, *left_elbow, *right_elbow, *centroid;
  SkeltrackJointList list;

  list = (SkeltrackJointList) current_skeleton->data;
  if (list == NULL)
    return;

  head = skeltrack_joint_list_get_joint (list,
                                         SKELTRACK_JOINT_ID_HEAD);
  left_hand = skeltrack_joint_list_get_joint (list,
                                              SKELTRACK_JOINT_ID_LEFT_HAND);
  right_hand = skeltrack_joint_list_get_joint (list,
                                               SKELTRACK_JOINT_ID_RIGHT_HAND);
  left_shoulder = skeltrack_joint_list_get_joint (list,
                                       SKELTRACK_JOINT_ID_LEFT_SHOULDER);
  right_shoulder = skeltrack_joint_list_get_joint (list,
                                       SKELTRACK_JOINT_ID_RIGHT_SHOULDER);
  shoulder_center = skeltrack_joint_list_get_joint (list,
                                       SKELTRACK_JOINT_ID_SHOULDER_CENTER);
  left_elbow = skeltrack_joint_list_get_joint (list,
                                               SKELTRACK_JOINT_ID_LEFT_ELBOW);
  right_elbow = skeltrack_joint_list_get_joint (list,
                                                SKELTRACK_JOINT_ID_RIGHT_ELBOW);
  centroid = skeltrack_joint_list_get_joint (list,
                                             SKELTRACK_JOINT_ID_CENTER);

  /* Paint it white */
  clutter_cairo_texture_clear (texture);
  clutter_cairo_texture_get_surface_size (texture, &width, &height);
  color = clutter_color_new (255, 255, 255, 255);
  clutter_cairo_set_source_color (cairo, color);
  cairo_rectangle (cairo, 0, 0, width, height);
  cairo_fill (cairo);
  clutter_color_free (color);


  connect_joints (cairo, head, shoulder_center, "#afafaf");

  paint_joint (cairo, head, 50000, "#FFF800");

  connect_joints (cairo, shoulder_center, centroid, "#afafaf");

  connect_joints (cairo, left_shoulder, shoulder_center, "#afafaf");

  connect_joints (cairo, left_shoulder, left_elbow, "#afafaf");

  connect_joints (cairo, right_shoulder, shoulder_center, "#afafaf");

  connect_joints (cairo, right_shoulder, right_elbow, "#afafaf");

  connect_joints (cairo, right_hand, right_elbow, "#afafaf");

  connect_joints (cairo, left_hand, left_elbow, "#afafaf");

  paint_joint (cairo, left_hand, 30000, "#C2FF00");

  paint_joint (cairo, right_hand, 30000, "#00FAFF");

}

static void
set_info_text (void)
{
  gchar *title;
  gchar *frame_file_name;

  frame_file_name = (gchar *) g_list_nth_data (frame_path_list,
                                               current_frame_number-1);

  title = g_strdup_printf( "<b>Threshold:</b> %d\t\t\t\t"
                           "<b>Frame:</b> %d - %s\n"
                           "<b>Smoothing Enabled:</b> %s\t\t\t"
                           "<b>Smoothing Level:</b> %.2f\t\t\t",
                           THRESHOLD_END,
                           current_frame_number,
                           frame_file_name? frame_file_name : "",
                           ENABLE_SMOOTHING ? "Yes" : "No",
                           SMOOTHING_FACTOR
                           );
  clutter_text_set_markup (CLUTTER_TEXT (info_text), title);
  g_free (title);
}

static void
set_threshold (gint difference)
{
  gint new_threshold = THRESHOLD_END + difference;
  if (new_threshold >= THRESHOLD_BEGIN + 300 &&
      new_threshold <= 8000)
    THRESHOLD_END = new_threshold;
}

static void
enable_smoothing (gboolean enable)
{
  if (skeleton != NULL)
    g_object_set (skeleton, "enable-smoothing", enable, NULL);
}

static void
set_smoothing_factor (gfloat factor)
{
  if (skeleton != NULL)
    {
      SMOOTHING_FACTOR += factor;
      SMOOTHING_FACTOR = CLAMP (SMOOTHING_FACTOR, 0.0, 1.0);
      g_object_set (skeleton, "smoothing-factor", SMOOTHING_FACTOR, NULL);
    }
}

static gboolean
paint_depth (guchar *buffer, guint width, guint height)
{
  gchar *head_color, *left_shoulder_color, *right_shoulder_color,
        *shoulder_center_color, *left_elbow_color, *right_elbow_color,
        *left_hand_color, *right_hand_color, *centroid_color;

  head_color = "#ff0000";
  left_hand_color = "#00ff00";
  right_hand_color = "#0000ff";
  left_elbow_color = "#97FF93";
  right_elbow_color = "#9094FF";
  left_shoulder_color = "#125500";
  right_shoulder_color = "#000045";
  shoulder_center_color = "#000000";
  centroid_color = "#FFFB00";

  SkeltrackJoint *head, *left_hand, *right_hand, *shoulder_center,
    *left_shoulder, *right_shoulder, *left_elbow, *right_elbow, *centroid;
  SkeltrackJointList list;

  list = (SkeltrackJointList) current_skeleton->data;
  if (list == NULL)
    return;

  head = skeltrack_joint_list_get_joint (list,
                                         SKELTRACK_JOINT_ID_HEAD);
  left_hand = skeltrack_joint_list_get_joint (list,
                                              SKELTRACK_JOINT_ID_LEFT_HAND);
  right_hand = skeltrack_joint_list_get_joint (list,
                                               SKELTRACK_JOINT_ID_RIGHT_HAND);
  left_shoulder = skeltrack_joint_list_get_joint (list,
                                       SKELTRACK_JOINT_ID_LEFT_SHOULDER);
  right_shoulder = skeltrack_joint_list_get_joint (list,
                                       SKELTRACK_JOINT_ID_RIGHT_SHOULDER);
  shoulder_center = skeltrack_joint_list_get_joint (list,
                                       SKELTRACK_JOINT_ID_SHOULDER_CENTER);
  left_elbow = skeltrack_joint_list_get_joint (list,
                                               SKELTRACK_JOINT_ID_LEFT_ELBOW);
  right_elbow = skeltrack_joint_list_get_joint (list,
                                                SKELTRACK_JOINT_ID_RIGHT_ELBOW);
  centroid = skeltrack_joint_list_get_joint (list,
                                             SKELTRACK_JOINT_ID_CENTER);


  if (head)
    draw_point (buffer, width, height, head_color, head->screen_x,
        head->screen_y);
  if (left_hand)
    draw_point (buffer, width, height, left_hand_color, left_hand->screen_x,
        left_hand->screen_y);
  if (right_hand)
    draw_point (buffer, width, height, right_hand_color, right_hand->screen_x,
        right_hand->screen_y);
  if (left_shoulder)
    draw_point (buffer, width, height, left_shoulder_color, left_shoulder->screen_x,
        left_shoulder->screen_y);
  if (right_shoulder)
    draw_point (buffer, width, height, right_shoulder_color, right_shoulder->screen_x,
        right_shoulder->screen_y);
  if (shoulder_center)
    draw_point (buffer, width, height, shoulder_center_color, shoulder_center->screen_x,
        shoulder_center->screen_y);
  if (left_elbow)
    draw_point (buffer, width, height, left_elbow_color, left_elbow->screen_x,
        left_elbow->screen_y);
  if (right_elbow)
    draw_point (buffer, width, height, right_elbow_color, right_elbow->screen_x,
        right_elbow->screen_y);
  if (centroid)
    draw_point (buffer, width, height, centroid_color, centroid->screen_x,
        centroid->screen_y);
  GError *error = NULL;

  if (! clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (depth_tex),
        buffer,
        FALSE,
        width, height,
        0,
        3,
        CLUTTER_TEXTURE_NONE,
        &error))
  {
    g_debug ("Error setting texture area: %s", error->message);
    g_error_free (error);
    return FALSE;
  }

  return TRUE;
}

static void
track_video ()
{
  GError *error;
  gint i = 1;
  GList *frame;

  g_list_free_full (skeleton_list,
      (GDestroyNotify) skeltrack_joint_list_free);

  skeleton_list = NULL;

  for (frame = g_list_first (frame_list);
       frame != NULL;
       frame = g_list_next (frame))
    {

      guint16 *depth = (guint16 *) frame->data;

      BufferInfo *buffer_info = process_buffer (depth,
                                                width,
                                                height,
                                                dimension_reduction,
                                                THRESHOLD_BEGIN,
                                                THRESHOLD_END);

      g_printf ("Frame %d: ", i++);

      SkeltrackJointList pose =
        skeltrack_skeleton_track_joints_sync (skeleton,
                                              buffer_info->reduced_buffer,
                                              buffer_info->reduced_width,
                                              buffer_info->reduced_height,
                                              NULL,
                                              &error);

      skeleton_list = g_list_append (skeleton_list, pose);
      g_printf ("\n");
    }
}

static void
first_frame ()
{
  current_skeleton = g_list_first (skeleton_list);
  current_frame = g_list_first (frame_list);
  current_frame_number = 1;
}

static void
last_frame ()
{
  current_skeleton = g_list_last (skeleton_list);
  current_frame = g_list_last (frame_list);
  current_frame_number = g_list_length (frame_list);
}

static gboolean
next_frame ()
{
  GList *next_skeleton, *next_frame;

  next_skeleton = g_list_next (current_skeleton);
  next_frame = g_list_next (current_frame);

  if (next_skeleton != NULL && next_frame != NULL)
    {
      current_skeleton = next_skeleton;
      current_frame = next_frame;
      current_frame_number++;
      return TRUE;
    }

  return FALSE;
}

static gboolean
previous_frame ()
{
  GList *previous_skeleton, *previous_frame;

  previous_skeleton = g_list_previous (current_skeleton);
  previous_frame = g_list_previous (current_frame);

  if (previous_skeleton != NULL && previous_frame != NULL)
    {
      current_skeleton = previous_skeleton;
      current_frame = previous_frame;
      current_frame_number--;
      return TRUE;
    }

  return FALSE;
}

static guint16 *
cut_depth (guint16 *depth, guint threshold_begin, guint threshold_end)
{
  gint i, j;
  gint index, value;

  guint16 *thresholded_depth = g_slice_alloc0 (width * height * sizeof
      (guint16));

  for (i = 0; i < width; i++)
    {
      for (j = 0; j < height; j++)
        {
          index = j * width + i;
          value = depth[index];

          if (value > threshold_end || value < threshold_begin)
            {
              thresholded_depth[index] = 0;
            }
          else
            {
              thresholded_depth[index] = value;
            }
        }
    }

  return thresholded_depth;
}

static void
paint_frame ()
{
  guint16 *depth;
  guint16 *thresholded_depth;
  BufferInfo *buffer_info;

  depth = (guint16 *) current_frame->data;

  thresholded_depth = cut_depth (depth, THRESHOLD_BEGIN, THRESHOLD_END);

  guchar *grayscale_buffer = create_grayscale_buffer (thresholded_depth, width, height);

  paint_depth (grayscale_buffer, width, height);

  g_slice_free1 (sizeof (guchar) * width * height * 3, grayscale_buffer);

  clutter_cairo_texture_invalidate (CLUTTER_CAIRO_TEXTURE (skeleton_tex));

  g_slice_free1 (width * height * sizeof (guint16), thresholded_depth);
}

static gboolean
on_key_press (ClutterActor *actor,
              ClutterEvent *event,
              gpointer data)
{
  gdouble angle;
  guint key;
  guint aux;
  g_return_val_if_fail (event != NULL, FALSE);

  key = clutter_event_get_key_symbol (event);
  switch (key)
    {
    case CLUTTER_KEY_space:
      track_video ();
      first_frame ();
      paint_frame ();
      break;
    case CLUTTER_KEY_plus:
      set_threshold (100);
      break;
    case CLUTTER_KEY_minus:
      set_threshold (-100);
      break;
    case CLUTTER_KEY_s:
      ENABLE_SMOOTHING = !ENABLE_SMOOTHING;
      enable_smoothing (ENABLE_SMOOTHING);
      break;
    case CLUTTER_KEY_k:
      if (next_frame())
          paint_frame ();
      break;
    case CLUTTER_KEY_j:
      if (previous_frame())
          paint_frame ();
      break;
    case CLUTTER_KEY_r:
      first_frame ();
      paint_frame ();
      break;
    case CLUTTER_KEY_t:
      last_frame ();
      paint_frame ();
      break;
    case CLUTTER_KEY_o:
      aux = width;
      width = height;
      height = aux;
      set_orientation ();
      break;
    case CLUTTER_KEY_Right:
      set_smoothing_factor (.05);
      break;
    case CLUTTER_KEY_Left:
      set_smoothing_factor (-.05);
      break;
    }
  set_info_text ();
  return TRUE;
}

static ClutterActor *
create_instructions (void)
{
  ClutterActor *text;

  text = clutter_text_new ();
  clutter_text_set_markup (CLUTTER_TEXT (text),
                         "<b>Instructions:</b>\n"
                         "\tTrack joints:  \t\t\t\tSpace bar\t\t\t"
                         "\tNext frame:  \t\t\tk\n"
                         "\tIncrease threshold:  \t\t\t+/-\t\t\t\t"
                         "\tPrevious frame:  \t\t\tj\n"
                         "\tEnable/Disable smoothing:  \t\ts\t\t\t\t"
                         "\tRewind:  \t\t\t\tr\n"
                         "\tSet smoothing level:  \t\t\tLeft/Right Arrows\t\t"
                         "\tGo to last frame:   \t\tt\n"
                         "\tChange orientation:   \t\t\to"
                           );
  return text;
}

static void
on_destroy (ClutterActor *actor, gpointer data)
{
  clutter_main_quit ();
}

static void
init ()
{
  ClutterActor *stage;

  stage = clutter_stage_get_default ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Skeltrack Video Player");
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);

  g_signal_connect (stage, "destroy", G_CALLBACK (on_destroy), NULL);
  g_signal_connect (stage,
                    "key-press-event",
                    G_CALLBACK (on_key_press),
                    NULL);

  skeleton_tex = clutter_cairo_texture_new (width, height);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), skeleton_tex);

  depth_tex = clutter_cairo_texture_new (width, height);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), depth_tex);

  info_text = clutter_text_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), info_text);

  instructions = create_instructions ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), instructions);

  clutter_actor_show_all (stage);

  skeleton = SKELTRACK_SKELETON (skeltrack_skeleton_new ());
  g_object_get (skeleton, "smoothing-factor", &SMOOTHING_FACTOR, NULL);

  set_orientation ();

  set_info_text ();
  g_signal_connect (skeleton_tex,
                    "draw",
                    G_CALLBACK (on_texture_draw),
                    NULL);
}

static void
quit (gint signale)
{
  signal (SIGINT, 0);

  clutter_main_quit ();
}

static void
free_depth_buffer (guint16 *buffer, gpointer unused)
{
  guint count = width * height * sizeof (guint16);
  g_slice_free1 (count, buffer);
}

int
main (int argc, char *argv[])
{
  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return -1;

  gchar *directory;

  init();

  signal (SIGINT, quit);

  if (argc < 3)
    {
      g_print ("Usage: %s VIDEO_DIRECTORY DIMENSION_REDUCTION\n", argv[0]);
      return 0;
    }

  directory = argv[1];
  dimension_reduction = atoi(argv[2]);

  read_video (directory, width, height, dimension_reduction);

  clutter_main ();

  g_list_free_full (skeleton_list, (GDestroyNotify) skeltrack_joint_list_free);
  g_list_free_full (frame_list, (GDestroyNotify) free_depth_buffer);

  if (skeleton != NULL)
    {
      g_object_unref (skeleton);
    }

  return 0;
}

