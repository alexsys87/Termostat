#include "test.h"
#include "menu_system.h"
#include "regulator.h"

// LCD stub: remembers the last printed lines
static char lcd_lines[LCD_ROWS][LCD_COLS + 1];
void HD44780_print_line(HD44780* lcd, uint8_t row, const char* str) {
    (void)lcd;
    snprintf(lcd_lines[row], sizeof(lcd_lines[row]), "%-8.8s", str);
}

static int16_t temp = 250;
static int16_t delta = 10;
static uint8_t u8 = 0;
static uint16_t u16 = 29950;
static uint16_t ki = 100;
static uint32_t counter = 4000000000u;
static const char* const LABELS[] = {"Auto", "Man On", "Man Off"};

static const param_desc_t P_TEMP_ = {&temp, NULL, -550, 1250, 5, PARAM_TEMP, 1, 0, UNIT_NONE};
static const param_desc_t P_DELTA = {&delta, NULL, 1, 200, 1, PARAM_TDELTA, 1, 0, UNIT_NONE};
static const param_desc_t P_ENUM_ = {&u8, LABELS, 0, 2, 1, PARAM_U8, 0, 0, UNIT_NONE};
static const param_desc_t P_U16_  = {&u16, NULL, 100, 30000, 100, PARAM_U16, 0, 0, UNIT_MS};
static const param_desc_t P_KI    = {&ki, NULL, 0, 30000, 1, PARAM_U16, 3, 0, UNIT_NONE};
static const param_desc_t P_COUNT = {&counter, NULL, 0, 0, 0, PARAM_U32, 0, PF_READONLY, UNIT_M};

static const char* fmt(const param_desc_t* p) {
    static char buf[16];
    param_format(p, buf);
    return buf;
}

static void test_format(void) {
    char b[16];
    fmt_fixed(b, -5, 1);    CHECK_STR(b, "-0.5");
    fmt_fixed(b, 253, 1);   CHECK_STR(b, "25.3");
    fmt_fixed(b, 5, 3);     CHECK_STR(b, "0.005");
    fmt_fixed(b, -1250, 1); CHECK_STR(b, "-125.0");
    fmt_fixed(b, 100, 0);   CHECK_STR(b, "100");
    fmt_uint(b, 0);         CHECK_STR(b, "0");
    fmt_temp(b, TEMP_INVALID, false); CHECK_STR(b, "---");
}

static void test_params(void) {
    param_fahrenheit = false;
    temp = 250;
    CHECK_STR(fmt(&P_TEMP_), "25.0C");
    CHECK(param_step(&P_TEMP_, 1, 1));
    CHECK_STR(fmt(&P_TEMP_), "25.5C");
    temp = 1248;
    param_step(&P_TEMP_, 1, 10);
    CHECK_EQ(temp, 1250);
    CHECK(!param_step(&P_TEMP_, 1, 1));    // already at the limit

    // Fahrenheit: stored in Celsius, shown in Fahrenheit
    param_fahrenheit = true;
    temp = 1000;
    CHECK_STR(fmt(&P_TEMP_), "212.0F");
    delta = 10;
    CHECK_STR(fmt(&P_DELTA), "1.8F");
    param_fahrenheit = false;

    // Enumerations wrap around
    u8 = 0;
    CHECK_STR(fmt(&P_ENUM_), "Auto");
    param_step(&P_ENUM_, -1, 1);
    CHECK_STR(fmt(&P_ENUM_), "Man Off");
    param_step(&P_ENUM_, 1, 1);
    CHECK_STR(fmt(&P_ENUM_), "Auto");
    u8 = 7;
    CHECK_STR(fmt(&P_ENUM_), "Auto");      // invalid stored value

    // Acceleration and clamping, units
    param_step(&P_U16_, 1, 10);
    CHECK_STR(fmt(&P_U16_), "30000ms");

    // Fixed point with 3 decimals
    CHECK_STR(fmt(&P_KI), "0.100");
    param_step(&P_KI, -1, 1);
    CHECK_STR(fmt(&P_KI), "0.099");

    // Read-only counters
    CHECK_STR(fmt(&P_COUNT), "4000000000m");
    CHECK(!param_step(&P_COUNT, 1, 1));

    // Clamping after loading from Flash
    temp = 3000;
    param_clamp(&P_TEMP_);
    CHECK_EQ(temp, 1250);
    delta = -5;
    param_clamp(&P_DELTA);
    CHECK_EQ(delta, 1);
}

static int actions = 0;
static void action(void) { actions++; }

static const menu_item_t sub_menu[] = {
    MENU_PARAM("Setp", P_TEMP_),
    MENU_ACTION("Save", action),
    MENU_BACK("Back")
};
static const menu_item_t root_menu[] = {
    MENU_NODE("Basic", sub_menu),
    MENU_NODE("PID", sub_menu),
    MENU_NODE("Adv", sub_menu),
    MENU_BACK("Back")
};

static void test_navigation(void) {
    menu_state_t m;
    HD44780 lcd;

    menu_open(&m, root_menu, COUNT_OF(root_menu));
    menu_handle_down(&m, 1);
    menu_handle_down(&m, 1);
    menu_draw(&m, &lcd);
    CHECK_STR(lcd_lines[0], " PID    ");
    CHECK_STR(lcd_lines[1], ">Adv    ");

    // Into the submenu and edit the parameter
    CHECK_EQ(menu_handle_enter(&m), MENU_EVT_NONE);
    CHECK(m.current_menu == sub_menu);
    CHECK_EQ(menu_handle_enter(&m), MENU_EVT_EDIT_BEGIN);
    CHECK(menu_edit_param(&m) == &P_TEMP_);
    temp = 200;
    CHECK_EQ(menu_handle_up(&m, 1), MENU_EVT_CHANGED);
    menu_draw(&m, &lcd);
    CHECK_STR(lcd_lines[0], "Setp    ");
    CHECK_STR(lcd_lines[1], "20.5C   ");
    CHECK_EQ(menu_handle_enter(&m), MENU_EVT_EDIT_END);
    CHECK(menu_edit_param(&m) == NULL);

    // Action
    menu_handle_down(&m, 1);
    menu_handle_enter(&m);
    CHECK_EQ(actions, 1);

    // Back returns to the parent at the same position
    menu_handle_down(&m, 1);
    CHECK_EQ(menu_handle_enter(&m), MENU_EVT_NONE);
    CHECK(m.current_menu == root_menu);
    CHECK_EQ(m.selected_index, 2);

    // Wrap-around and Back on the top level closes the menu
    menu_handle_up(&m, 1);
    menu_handle_up(&m, 1);
    menu_handle_up(&m, 1);
    CHECK_EQ(m.selected_index, 3);
    CHECK_EQ(menu_handle_enter(&m), MENU_EVT_EXIT);
    CHECK(!menu_is_open(&m));

    // Clamping walks the whole tree
    temp = -9000;
    menu_clamp_params(root_menu, COUNT_OF(root_menu));
    CHECK_EQ(temp, -550);
}

int main(void) {
    test_format();
    test_params();
    test_navigation();
    return TEST_REPORT("menu_system");
}
