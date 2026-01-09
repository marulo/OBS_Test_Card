#include "test-card-dock.h"
#include <QHBoxLayout>
#include <QStyle>

TestCardDock::TestCardDock(QWidget *parent)
    : QDialog(parent), globalSource(nullptr), isEnabled(false) {
  setWindowTitle("Test Card Control");

  // Create buttons
  toggleButton = new QPushButton("TEST CARD", this);
  toggleButton->setCheckable(true);
  toggleButton->setMinimumWidth(120);

  settingsButton = new QPushButton("⋮", this);
  settingsButton->setMaximumWidth(30);
  settingsButton->setToolTip("Settings");

  // Create layout
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(4);
  layout->addWidget(toggleButton);
  layout->addWidget(settingsButton);
  layout->addStretch();

  setLayout(layout);

  // Connect signals
  connect(toggleButton, &QPushButton::clicked, this,
          &TestCardDock::onToggleClicked);
  connect(settingsButton, &QPushButton::clicked, this,
          &TestCardDock::onSettingsClicked);

  // Create global source
  createGlobalSource();
  updateButtonState();
}

TestCardDock::~TestCardDock() {
  if (globalSource) {
    obs_source_release(globalSource);
  }
}

void TestCardDock::createGlobalSource() {
  // Check if source already exists
  globalSource = obs_get_source_by_name("__Test_Card_Global__");

  if (!globalSource) {
    // Create new source with correct ID
    obs_data_t *settings = obs_data_create();
    globalSource = obs_source_create("test_source", "__Test_Card_Global__",
                                     settings, nullptr);
    obs_data_release(settings);
  }
}

void TestCardDock::onToggleClicked() {
  if (!globalSource) {
    createGlobalSource();
    if (!globalSource)
      return;
  }

  isEnabled = toggleButton->isChecked();

  if (isEnabled) {
    // Add to current scene
    obs_source_t *scene = obs_frontend_get_current_scene();
    if (scene) {
      obs_scene_t *obs_scene = obs_scene_from_source(scene);
      if (obs_scene) {
        // Check if already added
        obs_sceneitem_t *existing =
            obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
        if (!existing) {
          // Add as scene item
          obs_sceneitem_t *item = obs_scene_add(obs_scene, globalSource);
          if (item) {
            // Position at top-left, fullscreen
            struct vec2 pos = {0, 0};
            obs_sceneitem_set_pos(item, &pos);

            // Get canvas size for scale
            obs_video_info ovi;
            if (obs_get_video_info(&ovi)) {
              uint32_t source_w = obs_source_get_width(globalSource);
              uint32_t source_h = obs_source_get_height(globalSource);

              if (source_w > 0 && source_h > 0) {
                struct vec2 scale;
                scale.x = (float)ovi.base_width / (float)source_w;
                scale.y = (float)ovi.base_height / (float)source_h;
                obs_sceneitem_set_scale(item, &scale);
              }
            }
          }
        }
      }
      obs_source_release(scene);
    }
  } else {
    // Remove from current scene
    obs_source_t *scene = obs_frontend_get_current_scene();
    if (scene) {
      obs_scene_t *obs_scene = obs_scene_from_source(scene);
      if (obs_scene) {
        // Find and remove our source
        obs_sceneitem_t *item =
            obs_scene_find_source(obs_scene, obs_source_get_name(globalSource));
        if (item) {
          obs_sceneitem_remove(item);
        }
      }
      obs_source_release(scene);
    }
  }

  updateButtonState();
}

void TestCardDock::onSettingsClicked() {
  if (!globalSource)
    return;

  obs_frontend_open_source_properties(globalSource);
}

void TestCardDock::updateButtonState() {
  if (isEnabled) {
    toggleButton->setStyleSheet(
        "background-color: #4CAF50; color: white; font-weight: bold;");
  } else {
    toggleButton->setStyleSheet("");
  }
}
