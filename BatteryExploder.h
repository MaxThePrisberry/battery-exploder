/**************************************************************************/
/* LabWindows/CVI User Interface Resource (UIR) Include File              */
/*                                                                        */
/* WARNING: Do not add to, delete from, or otherwise modify the contents  */
/*          of this include file.                                         */
/**************************************************************************/

#include <userint.h>

#ifdef __cplusplus
    extern "C" {
#endif

     /* Panels and Controls: */

#define  PANEL                            1       /* callback function: PanelCallback */
#define  PANEL_BTN_CMD_PROMPT_SEND        2       /* control type: command, callback function: CmdPromptSendCallback */
#define  PANEL_BTN_DTB_2_RUN_STOP         3       /* control type: command, callback function: DTB2RunStopCallback */
#define  PANEL_BTN_DTB_1_RUN_STOP         4       /* control type: command, callback function: DTB1RunStopCallback */
#define  PANEL_LED_REMOTE_MODE            5       /* control type: LED, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_V           6       /* control type: numeric, callback function: (none) */
#define  PANEL_MFLOW_STATUS               7       /* control type: string, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_V        8       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_CHARGE_I           9       /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_SET_DISCHARGE_I        10      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_POWER                  11      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CURRENT                12      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_VOLTAGE                13      /* control type: numeric, callback function: (none) */
#define  PANEL_TOGGLE_REMOTE_MODE         14      /* control type: binary, callback function: RemoteModeToggle */
#define  PANEL_BTN_TEST_QUEUE             15      /* control type: command, callback function: TestDeviceQueueCallback */
#define  PANEL_BTN_TEST_PSB               16      /* control type: command, callback function: TestPSBCallback */
#define  PANEL_STR_PSB_STATUS             17      /* control type: string, callback function: (none) */
#define  PANEL_BTN_TEST_TEMP_RAMP         18      /* control type: command, callback function: TestDTBRampSoakCallback */
#define  PANEL_BTN_TEST_ALICAT            19      /* control type: command, callback function: TestALICATCallback */
#define  PANEL_BTN_TEST_BIOLOGIC          20      /* control type: command, callback function: TestBiologicCallback */
#define  PANEL_STR_BIOLOGIC_STATUS        21      /* control type: string, callback function: (none) */
#define  PANEL_DEC_BAT_CONSTS             22      /* control type: deco, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL_2         23      /* control type: textMsg, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL_3         24      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_BIOLOGIC_STATUS        25      /* control type: LED, callback function: (none) */
#define  PANEL_LED_PSB_STATUS             26      /* control type: LED, callback function: (none) */
#define  PANEL_DEC_STATUS                 27      /* control type: deco, callback function: (none) */
#define  PANEL_CONTROL_LABEL              28      /* control type: textMsg, callback function: (none) */
#define  PANEL_STATUS_LABEL               29      /* control type: textMsg, callback function: (none) */
#define  PANEL_CMD_PROMPT_TEXTBOX         30      /* control type: textBox, callback function: (none) */
#define  PANEL_OUTPUT_TEXTBOX             31      /* control type: textBox, callback function: (none) */
#define  PANEL_EXPERIMENTS                32      /* control type: tab, callback function: (none) */
#define  PANEL_DEC_MANUAL_CONTROL         33      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_2                    34      /* control type: graph, callback function: (none) */
#define  PANEL_DEC_GRAPHS                 35      /* control type: deco, callback function: (none) */
#define  PANEL_GRAPH_1                    36      /* control type: graph, callback function: (none) */
#define  PANEL_GRAPH_BIOLOGIC             37      /* control type: graph, callback function: (none) */
#define  PANEL_BAT_CONSTS_LABEL           38      /* control type: textMsg, callback function: (none) */
#define  PANEL_LED_DTB_2_STATUS           39      /* control type: LED, callback function: (none) */
#define  PANEL_LED_DTB_1_STATUS           40      /* control type: LED, callback function: (none) */
#define  PANEL_SPLITTER                   41      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_3                 42      /* control type: splitter, callback function: (none) */
#define  PANEL_SPLITTER_2                 43      /* control type: splitter, callback function: (none) */
#define  PANEL_STR_DTB_2_STATUS           44      /* control type: string, callback function: (none) */
#define  PANEL_STR_DTB_1_STATUS           45      /* control type: string, callback function: (none) */
#define  PANEL_NUM_DTB_2_SETPOINT         46      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_ALICAT_SETPOINT        47      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DTB_1_SETPOINT         48      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_DTB_2_TEMPERATURE      49      /* control type: scale, callback function: (none) */
#define  PANEL_NUM_DTB_1_TEMPERATURE      50      /* control type: scale, callback function: (none) */
#define  PANEL_TOGGLE_TEENSY              51      /* control type: binary, callback function: TestTeensyCallback */
#define  PANEL_STR_CMD_PROMPT_INPUT       52      /* control type: string, callback function: CmdPromptInputCallback */
#define  PANEL_NUM_TC0                    53      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_CH0_VOLTAGE            54      /* control type: numeric, callback function: (none) */
#define  PANEL_NUM_TC1                    55      /* control type: numeric, callback function: (none) */
#define  PANEL_DEC_TMPCTRL                56      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_BIO_GRAPH              57      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_CMDPROMPT              58      /* control type: deco, callback function: (none) */
#define  PANEL_MASS_FLOW                  59      /* control type: deco, callback function: (none) */
#define  PANEL_DEC_TCS                    60      /* control type: deco, callback function: (none) */
#define  PANEL_MFLOW_DIAL                 61      /* control type: scale, callback function: (none) */

#define  PANEL_LOAD                       2
#define  PANEL_LOAD_IMG_LOGO              2       /* control type: picture, callback function: (none) */

     /* tab page panel controls */
#define  RUNAWAY_FINAL_TEMP_RWY           2       /* control type: numeric, callback function: (none) */
#define  RUNAWAY_INITIAL_TEMP_RWY         3       /* control type: numeric, callback function: (none) */
#define  RUNAWAY_RAMP_RATE_RWY            4       /* control type: numeric, callback function: (none) */
#define  RUNAWAY_NUM_EIS_INTERVAL_RWY     5       /* control type: numeric, callback function: (none) */
#define  RUNAWAY_BTN_SIGNAL_RUNAWAY       6       /* control type: command, callback function: SignalRunawayReachedCallback */
#define  RUNAWAY_BTN_RWY                  7       /* control type: command, callback function: StartTempRampExperimentCallback */
#define  RUNAWAY_STR_RWY_STATUS           8       /* control type: string, callback function: (none) */
#define  RUNAWAY_TEMP_RAMP_NUM_OUTPUT     9       /* control type: numeric, callback function: (none) */
#define  RUNAWAY_CBX_AUTO_TUNE            10      /* control type: radioButton, callback function: (none) */
#define  RUNAWAY_CBX_ENABLE_EIS           11      /* control type: radioButton, callback function: (none) */
#define  RUNAWAY_CBX_CONT_TRAMP_EIS       12      /* control type: radioButton, callback function: (none) */
#define  RUNAWAY_RING_RAMP_MODE           13      /* control type: ring, callback function: (none) */


     /* Control Arrays: */

#define  BATTERY_CONSTANTS_ARR            1
#define  DTB_CONTROL_ARR                  2
#define  GRAPHS_ARR                       3
#define  MANUAL_CONTROL_ARR               4
#define  STATUS_ARR                       5

     /* Menu Bars, Menus, and Menu Items: */

#define  MENUBAR                          1
#define  MENUBAR_MENU1                    2
#define  MENUBAR_MENU2                    3


     /* Callback Prototypes: */

int  CVICALLBACK CmdPromptInputCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK CmdPromptSendCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK DTB1RunStopCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK DTB2RunStopCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK PanelCallback(int panel, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK RemoteModeToggle(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK SignalRunawayReachedCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK StartTempRampExperimentCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestALICATCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestBiologicCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestDeviceQueueCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestDTBRampSoakCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestPSBCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);
int  CVICALLBACK TestTeensyCallback(int panel, int control, int event, void *callbackData, int eventData1, int eventData2);


#ifdef __cplusplus
    }
#endif