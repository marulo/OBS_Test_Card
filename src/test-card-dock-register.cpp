#include "test-card-dock.h"

#ifdef ENABLE_QT

#include <QAction>
#include <obs-frontend-api.h>


static TestCardDock *s_dockInstance = nullptr;

extern "C" void register_test_card_dock() {
  // Wait for OBS to finish loading
  obs_frontend_add_event_callback(
      [](enum obs_frontend_event event, void *) {
        if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
          // Add menu action in Tools
          QAction *action = (QAction *)obs_frontend_add_tools_menu_qaction(
              "Test Card Control");

          // Connect to show/hide dialog
          QObject::connect(action, &QAction::triggered, []() {
            if (!s_dockInstance) {
              s_dockInstance =
                  new TestCardDock((QWidget *)obs_frontend_get_main_window());
            }
            s_dockInstance->show();
            s_dockInstance->raise();
            s_dockInstance->activateWindow();
          });
        } else if (event == OBS_FRONTEND_EVENT_EXIT) {
          if (s_dockInstance) {
            delete s_dockInstance;
            s_dockInstance = nullptr;
          }
        }
      },
      nullptr);
}

#else

extern "C" void register_test_card_dock() {
  // Qt not enabled, do nothing
}

#endif
