#include "ui_theme.h"

static const UiTheme dark = {
    .frame={0,0,0,255},.panel={18,18,18,255},.elevated={31,31,31,255},
    .hover={42,42,42,255},.pressed={25,25,25,255},
    .text={255,255,255,255},.subdued={179,179,179,255},
    .accent={30,215,96,255},.accent_hover={59,228,119,255},.accent_pressed={26,188,84,255},
    .on_accent={0,0,0,255},.negative={243,114,127,255},.warning={255,164,43,255},
    .information={83,157,245,255},.outline={124,124,124,255},.decorative={41,41,41,255},
    .tinted={255,255,255,26},.tinted_hover={255,255,255,36},.tinted_pressed={255,255,255,54},
    .focus={255,255,255,255},
    .space={2,4,6,8,12,16,24,32,48,64,96,128},.radius={2,4,6,8,16},
    .control={32,48,56},.icon={12,16,24,32,40,48,64,88},
    .duration_ms={50,100,150,200,250,300},.body=16,.small_text=14,.marginal=12,
    .title=24,.large_title=32,.headline=64,.focus_width=2,
    .disabled_opacity=0.3f,.active_opacity=0.7f,.hover_scale=1.04f
};
const UiTheme *ui_theme(void) { return &dark; }
Clay_Color ui_theme_opacity(Clay_Color color, float opacity)
{
    if (!(opacity >= 0)) { opacity=0; }
    if (opacity>1) { opacity=1; }
    color.a*=opacity; return color;
}
