#include "test-card-dock.h"
#include <obs-module.h>

// ---------------------------------------------------------------------------
// Scene item helpers
// ---------------------------------------------------------------------------

static bool remove_test_items_cb(obs_scene_t *, obs_sceneitem_t *item, void *)
{
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (src && strcmp(obs_source_get_id(src), "test_source") == 0)
		obs_sceneitem_remove(item);

	if (obs_sceneitem_is_group(item)) {
		obs_scene_t *group = obs_sceneitem_group_get_scene(item);
		if (group)
			obs_scene_enum_items(group, remove_test_items_cb, nullptr);
	}
	return true;
}

static bool cleanup_all_scenes_cb(void *, obs_source_t *source)
{
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene)
		obs_scene_enum_items(scene, remove_test_items_cb, nullptr);
	return true;
}

static void inject_into_scene(obs_source_t *scene_source, obs_source_t *card)
{
	if (!scene_source || !card)
		return;

	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		return;

	/* Check if the test card is already present */
	obs_sceneitem_t *existing = nullptr;
	auto find_cb = [](obs_scene_t *, obs_sceneitem_t *item, void *p) -> bool {
		obs_source_t *s = obs_sceneitem_get_source(item);
		if (s && strcmp(obs_source_get_id(s), "test_source") == 0) {
			*(obs_sceneitem_t **)p = item;
			return false;
		}
		return true;
	};
	obs_scene_enum_items(scene, find_cb, &existing);

	if (!existing) {
		obs_sceneitem_t *item = obs_scene_add(scene, card);
		if (item)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	} else {
		obs_sceneitem_set_visible(existing, true);
		obs_sceneitem_set_order(existing, OBS_ORDER_MOVE_TOP);
	}
}

/* Inject into the active scene behind the program transition (channel 0).
 * In "Duplicate Scene" studio mode, this is the PRIVATE duplicate that is
 * not reachable through obs_frontend_get_current_scene(). */
static void inject_into_program_output(obs_source_t *card)
{
	obs_source_t *transition = obs_get_output_source(0);
	if (!transition)
		return;

	obs_source_t *active = obs_transition_get_active_source(transition);
	if (active) {
		inject_into_scene(active, card);
		obs_source_release(active);
	}
	obs_source_release(transition);
}

static void remove_from_program_output(void)
{
	obs_source_t *transition = obs_get_output_source(0);
	if (!transition)
		return;

	obs_source_t *active = obs_transition_get_active_source(transition);
	if (active) {
		obs_scene_t *scene = obs_scene_from_source(active);
		if (scene)
			obs_scene_enum_items(scene, remove_test_items_cb, nullptr);
		obs_source_release(active);
	}
	obs_source_release(transition);
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
	settingsButton->setToolTip("Open test card settings");

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

	if (globalSource && !isEnabled)
		obs_source_set_enabled(globalSource, false);
}

// ---------------------------------------------------------------------------
// Activation / Deactivation
// ---------------------------------------------------------------------------

void TestCardDock::activateTestCard()
{
	if (!globalSource) {
		createGlobalSource();
		if (!globalSource)
			return;
	}

	obs_source_set_enabled(globalSource, true);

	/* Inject into every base scene (covers Preview in studio mode) */
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++)
		inject_into_scene(scenes.sources.array[i], globalSource);
	obs_frontend_source_list_free(&scenes);

	/* In Studio Mode the Program output may render a private duplicate
	 * scene that is not in the base scene list.  Reach it via channel 0. */
	if (obs_frontend_preview_program_mode_active())
		inject_into_program_output(globalSource);

	blog(LOG_INFO, "[TestCardDock] Test card activated");
}

void TestCardDock::deactivateTestCard()
{
	if (globalSource)
		obs_source_set_enabled(globalSource, false);

	cleanupAllScenes();

	if (obs_frontend_preview_program_mode_active())
		remove_from_program_output();

	blog(LOG_INFO, "[TestCardDock] Test card deactivated");
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void TestCardDock::cleanupAllScenes()
{
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
		if (scene)
			obs_scene_enum_items(scene, remove_test_items_cb, nullptr);
	}
	obs_frontend_source_list_free(&scenes);

	obs_enum_all_sources(cleanup_all_scenes_cb, nullptr);
}

// ---------------------------------------------------------------------------
// Frontend event handler
// ---------------------------------------------------------------------------

void TestCardDock::onFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	TestCardDock *dock = static_cast<TestCardDock *>(ptr);

	switch (event) {
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		if (!dock->globalSource)
			dock->createGlobalSource();
		if (dock->isEnabled)
			dock->activateTestCard();
		else
			dock->cleanupAllScenes();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (!dock->globalSource)
			dock->createGlobalSource();
		if (dock->isEnabled)
			dock->activateTestCard();
		else
			dock->cleanupAllScenes();
		break;

	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		/* A transition completed — inject into the new program scene
		 * (may be a fresh private duplicate in "Duplicate Scene" mode) */
		if (dock->isEnabled && dock->globalSource)
			inject_into_program_output(dock->globalSource);
		break;

	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		if (dock->isEnabled && dock->globalSource) {
			obs_source_t *preview = obs_frontend_get_current_preview_scene();
			if (preview) {
				inject_into_scene(preview, dock->globalSource);
				obs_source_release(preview);
			}
		}
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		if (dock->globalSource) {
			obs_source_release(dock->globalSource);
			dock->globalSource = nullptr;
		}
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// UI slots
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
