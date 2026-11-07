#pragma once

#define IDD_CROSSHAIR          400
#define IDD_DOT                401
#define IDD_ANIMATION          402
#define IDD_POSITION           403
#define IDD_HOTKEYS            404
#define IDD_SETTINGS           405

// Resource names deliberately differ from the application's existing ID_*
// constants; the numeric values remain compatible with the event handlers.
#define IDC_ENABLE            100
#define IDC_SIMULATE           101
#define IDC_RESET_DEFAULTS     102
#define IDC_EXIT               103
#define IDC_CROSS_COLOR        120
#define IDC_DOT_COLOR          121
#define IDC_DOT_ENABLED        122
#define IDC_ACTION             130
#define IDC_TRIGGER            131
#define IDC_EASING             132
#define IDC_MONITOR            140
#define IDC_ONLY_GAME          141
#define IDC_REFRESH_MONITORS   142
#define IDC_OFFSET_STEP        143
#define IDC_CURVE_X1           150
#define IDC_CURVE_Y1           151
#define IDC_CURVE_X2           152
#define IDC_CURVE_Y2           153
#define IDC_X_MINUS            160
#define IDC_X_PLUS             161
#define IDC_Y_MINUS            162
#define IDC_Y_PLUS             163
#define IDC_POINT1             170
#define IDC_POINT2             171
#define IDC_REPLAY             172
#define IDC_RESET_CURVE        173
#define IDC_CROSS_MODE         180
#define IDC_EDIT_CROSS         181
#define IDC_DOT_SHAPE          182
#define IDC_EDIT_DOT           183
#define IDC_LENGTH             200
#define IDC_GAP                201
#define IDC_THICKNESS          202
#define IDC_OPACITY            203
#define IDC_ANGLE              204
#define IDC_DOT_SIZE           205
#define IDC_DOT_OPACITY        206
#define IDC_CROSS_SIZE         207
#define IDC_ENTER_DURATION     210
#define IDC_LEAVE_DURATION     211
#define IDC_OFFSET_X           220
#define IDC_OFFSET_Y           221
#define IDC_CROSS_PREVIEW      300
#define IDC_DOT_PREVIEW        301
#define IDC_ANIMATION_PREVIEW  302
#define IDC_CURVE_GRAPH        303
#define IDC_STATUS             304
#define IDC_CROSS_SWATCH       305
#define IDC_DOT_SWATCH         306
#define IDC_EXTERNAL_PREVIEW   307
#define IDC_PROPERTY_APPLY     0x3021
#define IDC_HOTKEY_BASE        700
#define IDC_HOTKEY_CLEAR_BASE  720
#define IDC_RESET_HOTKEYS      740
#define IDC_HOTKEY_STATUS      741
#define IDC_SKIN               750

#define IDC_LENGTH_SLIDER          1200
#define IDC_GAP_SLIDER             1201
#define IDC_THICKNESS_SLIDER       1202
#define IDC_OPACITY_SLIDER         1203
#define IDC_ANGLE_SLIDER           1204
#define IDC_DOT_SIZE_SLIDER        1205
#define IDC_DOT_OPACITY_SLIDER     1206
#define IDC_CROSS_SIZE_SLIDER      1207
#define IDC_ENTER_DURATION_SLIDER  1210
#define IDC_LEAVE_DURATION_SLIDER  1211
#define IDC_OFFSET_X_SLIDER        1220
#define IDC_OFFSET_Y_SLIDER        1221

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif
