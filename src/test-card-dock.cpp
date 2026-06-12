#include "test-card-dock.h"
#include <QHBoxLayout>
#include <obs-module.h>
#include <graphics/graphics.h>

// ---------------------------------------------------------------------------
// Aggressive Cleanup Helper (Exterminates by ID)
// ---------------------------------------------------------------------------
static bool remove_stale_items_callback(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	bool *found_any = (bool *)param;
	obs_source_t *item_source = obs_sceneitem_get_source(item);

	if (item_source && strcmp(obs_source_get_id(item_source), "test_source") == 0) {
		obs_sceneitem_remove(item);
		if (found_any)
			*found_any = true;
	}

	if (obs_sceneitem_is_group(item)) {
		obs_scene_t *group_scene = obs_sceneitem_group_get_scene(item);
		if (group_scene) {
			obs_scene_enum_items(group_scene, remove_stale_items_callback, param);
		}
	}

	return true;
}

static bool cleanup_all_sources_callback(void *param, obs_source_t *source)
{
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene) {
		obs_scene_enum_items(scene, remove_stale_items_callback, param);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Scene Injector
// ---------------------------------------------------------------------------
static void inject_into_scene(obs_source_t *scene_source, obs_source_t *target_source)
{
	if (!scene_source || !target_source)
		return;
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene) {
		blog(LOG_INFO, "%s", "[TestCardDock] inject_into_scene: Failed because scene_source is not a scene!");
		return;
	}

	obs_sceneitem_t *found_item = nullptr;
	auto check_cb = [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
		obs_source_t *item_src = obs_sceneitem_get_source(item);
		if (item_src && strcmp(obs_source_get_id(item_src), "test_source") == 0) {
			*(obs_sceneitem_t **)param = item;
			return false; // stop iterating
		}
		return true;
	};
	obs_scene_enum_items(scene, check_cb, &found_item);

	if (!found_item) {
		obs_sceneitem_t *item = obs_scene_add(scene, target_source);
		if (item) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
			blog(LOG_INFO, "%s", "[TestCardDock] inject_into_scene: Added to scene");
		} else {
			blog(LOG_INFO, "%s", "[TestCardDock] inject_into_scene: Failed to add to scene");
		}
	} else {
		blog(LOG_INFO, "%s", "[TestCardDock] inject_into_scene: Already exists in scene");

		// Modify visibility and order OUTSIDE of the enumeration callback to prevent deadlocks
		obs_sceneitem_set_visible(found_item, true);
		obs_sceneitem_set_order(found_item, OBS_ORDER_MOVE_TOP);
		blog(LOG_INFO, "%s", "[TestCardDock] inject_into_scene: Forced visibility outside enumeration");
	}
}

// ---------------------------------------------------------------------------
// TestCardDock
// ---------------------------------------------------------------------------

TestCardDock::TestCardDock(QWidget *parent) : QDialog(parent), globalSource(nullptr), isEnabled(false)
{
	setWindowTitle("Test Card Control");

	toggleButton = new QPushButton("TEST CARD", this);
	toggleButton->setCheckable(true);
	toggleButton->setMinimumWidth(120);

	settingsButton = new QPushButton("Config", this);
	settingsButton->setToolTip("Settings");

	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);
	layout->addWidget(toggleButton);
	layout->addWidget(settingsButton);
	layout->addStretch();
	setLayout(layout);

	connect(toggleButton, &QPushButton::clicked, this, &TestCardDock::onToggleClicked);
	connect(settingsButton, &QPushButton::clicked, this, &TestCardDock::onSettingsClicked);

	obs_frontend_add_event_callback(onFrontendEvent, this);
	createGlobalSource();
	updateButtonState();
}

TestCardDock::~TestCardDock()
{
	obs_frontend_remove_event_callback(onFrontendEvent, this);

	if (isEnabled && globalSource) {
		obs_source_set_enabled(globalSource, false);
		isEnabled = false;
	}

	if (globalSource) {
		obs_source_release(globalSource);
		globalSource = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Source lifecycle
// ---------------------------------------------------------------------------

void TestCardDock::createGlobalSource()
{
	globalSource = obs_get_source_by_name("__Test_Card_Global__");

	if (!globalSource) {
		obs_data_t *settings = obs_data_create();
		globalSource = obs_source_create("test_source", "__Test_Card_Global__", settings, nullptr);
		obs_data_release(settings);
	}

	// Ensure it starts disabled
	if (globalSource && !isEnabled) {
		obs_source_set_enabled(globalSource, false);
	}
}

// ---------------------------------------------------------------------------
// Overlay Activation
// ---------------------------------------------------------------------------

void TestCardDock::activateTestCard()
{
	if (!globalSource) {
		createGlobalSource();
		if (!globalSource)
			return;
	}

	obs_source_set_enabled(globalSource, true);

	// 1. Inject into ALL base scenes (covers Preview in studio mode)
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	blog(LOG_INFO, "[TestCardDock] Injecting into %zu base scenes", scenes.sources.num);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		inject_into_scene(scenes.sources.array[i], globalSource);
	}
	obs_frontend_source_list_free(&scenes);

	// 2. In Studio Mode with "Duplicate Scene", the Program output renders
	//    a PRIVATE duplicate that is NOT in the base scene list.
	//    We reach it by going: Channel 0 -> Transition -> Active Source.
	if (obs_frontend_preview_program_mode_active()) {
		obs_source_t *transition = obs_get_output_source(0);
		if (transition) {
			obs_source_t *active = obs_transition_get_active_source(transition);
			if (active) {
				blog(LOG_INFO, "[TestCardDock] Program output active source: %s (id: %s)",
				     obs_source_get_name(active), obs_source_get_id(active));
				inject_into_scene(active, globalSource);
				obs_source_release(active);
			} else {
				blog(LOG_INFO, "%s", "[TestCardDock] No active source in program transition");
			}
			obs_source_release(transition);
		} else {
			blog(LOG_INFO, "%s", "[TestCardDock] No output source on channel 0");
		}
	}

	blog(LOG_INFO, "%s", "[TestCardDock] Test card ON");
}

void TestCardDock::deactivateTestCard()
{
	if (globalSource) {
		obs_source_set_enabled(globalSource, false);
	}

	// Clean from ALL scenes including any private duplicates
	cleanupStaleSceneItems();

	// Also clean from the program output's active source (private duplicate)
	if (obs_frontend_preview_program_mode_active()) {
		obs_source_t *transition = obs_get_output_source(0);
		if (transition) {
			obs_source_t *active = obs_transition_get_active_source(transition);
			if (active) {
				obs_scene_t *scene = obs_scene_from_source(active);
				if (scene) {
					obs_scene_enum_items(scene, remove_stale_items_callback, nullptr);
				}
				obs_source_release(active);
			}
			obs_source_release(transition);
		}
	}

	blog(LOG_INFO, "%s", "[TestCardDock] Test card OFF");
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void TestCardDock::cleanupStaleSceneItems()
{
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		if (scene) {
			obs_scene_enum_items(scene, remove_stale_items_callback, nullptr);
		}
	}
	obs_frontend_source_list_free(&scenes);

	obs_enum_all_sources(cleanup_all_sources_callback, nullptr);
}

// ---------------------------------------------------------------------------
// Event Handlers
// ---------------------------------------------------------------------------

void TestCardDock::onFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	TestCardDock *dock = static_cast<TestCardDock *>(ptr);

	if (event == OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED || event == OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED) {
		if (!dock->globalSource)
			dock->createGlobalSource();

		if (dock->isEnabled) {
			dock->activateTestCard();
		} else {
			dock->cleanupStaleSceneItems();
		}
	}

	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		if (!dock->globalSource)
			dock->createGlobalSource();

		if (dock->isEnabled) {
			dock->activateTestCard();
		} else {
			dock->cleanupStaleSceneItems();
		}
	}

	// When program scene changes (e.g. user transitions), inject into
	// the new program output scene (which may be a fresh private duplicate)
	if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		if (dock->isEnabled && dock->globalSource) {
			obs_source_t *transition = obs_get_output_source(0);
			if (transition) {
				obs_source_t *active = obs_transition_get_active_source(transition);
				if (active) {
					inject_into_scene(active, dock->globalSource);
					obs_source_release(active);
				}
				obs_source_release(transition);
			}
		}
	}

	// When preview scene changes, inject into the new preview scene
	if (event == OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED) {
		if (dock->isEnabled && dock->globalSource) {
			obs_source_t *preview = obs_frontend_get_current_preview_scene();
			if (preview) {
				inject_into_scene(preview, dock->globalSource);
				obs_source_release(preview);
			}
		}
	}

	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
		if (dock->globalSource) {
			obs_source_release(dock->globalSource);
			dock->globalSource = nullptr;
		}
	}
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void TestCardDock::onToggleClicked()
{
	isEnabled = toggleButton->isChecked();

	if (isEnabled)
		activateTestCard();
	else
		deactivateTestCard();

	updateButtonState();
}

void TestCardDock::onSettingsClicked()
{
	if (!globalSource)
		return;
	obs_frontend_open_source_properties(globalSource);
}

void TestCardDock::updateButtonState()
{
	if (isEnabled)
		toggleButton->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
	else
		toggleButton->setStyleSheet("");
}
