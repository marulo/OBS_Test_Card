static void test_source_update(void *data, obs_data_t *settings) {
  struct test_source_data *src = data;

  // Save old values to detect changes
  int old_cell_size = src->cell_size;
  uint32_t old_dark = src->bg_dark_color;
  uint32_t old_light = src->bg_light_color;

  src->cell_size = (int)obs_data_get_int(settings, "cell_size");
  if (src->cell_size <= 0)
    src->cell_size = CELL_SIZE;

  // Update background colors
  src->bg_dark_color =
      0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_dark_color");
  src->bg_light_color =
      0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_light_color");

  // If colors or size changed, force grid regeneration
  if (old_dark != src->bg_dark_color || old_light != src->bg_light_color ||
      old_cell_size != src->cell_size) {
    // Free old grid to force resize_grid() to regenerate with new colors
    if (src->grid_colors) {
      bfree(src->grid_colors);
      src->grid_colors = NULL;
    }
    src->dirty |= DIRTY_STATIC; // Force regeneration
  }
}
