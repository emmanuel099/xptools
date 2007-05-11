#ifndef WED_MENUS_H
#define WED_MENUS_H

#include "GUI_Application.h"
#include "GUI_Menus.h"

extern	GUI_Menu	test1;
extern	GUI_Menu	sub1;

enum {

	// File Menu
	wed_NewPackage = GUI_APP_MENUS,
	wed_OpenPackage,
	// View menu
	wed_PickOverlay,
	wed_ToggleOverlay,
	wed_ToggleTerraserver,
	// Create Menu
	wed_Group,
	wed_Ungroup,
	// Airport Menu
	wed_CreateApt,
	wed_EditApt

};

class	GUI_Application;

void WED_MakeMenus(GUI_Application * inApp);

#endif
