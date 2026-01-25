'''
Docstring for Firmware.main
Hi, this is the firmware of my macropad named Codemate v1, 
'''


import board
from kmk.kmk_keyboard import KMKKeyboard
from kmk.keys import KC
from kmk.scanners import DiodeOrientation
from kmk.modules.layers import Layers
from kmk.modules.encoder import EncoderHandler
from kmk.modules.debounce import Debounce
from kmk.extensions.RGB import RGB, AnimationModes
from kmk.extensions.media_keys import MediaKeys

# oled stuff - keeping this optional bc i might not solder it right away
try:
    from kmk.extensions.display import Display, TextEntry
    from kmk.extensions.display.ssd1306 import SSD1306
    oled_working = True
except:
    oled_working = False

keyboard = KMKKeyboard()
keyboard.extensions.append(MediaKeys())

# matrix setup - 3x3 grid
# cols: D3, D6, D7
# rows: D0, D1, D2
keyboard.col_pins = (board.D3, board.D6, board.D7)
keyboard.row_pins = (board.D0, board.D1, board.D2)
keyboard.diode_orientation = DiodeOrientation.COL2ROW

# proper debounce setup
debounce = Debounce()
debounce.debounce_time = 10  # 10ms for mx switches
keyboard.modules.append(debounce)

# rgb leds on D10, got 6 of them chained one after another by connecting one's Dout to another's DIN
rgb = RGB(
    pixel_pin=board.D10,
    num_pixels=6,
    animation_mode=AnimationModes.STATIC_STANDBY,
    brightness=0.6,  # not too bright, hurts eyes at night
    brightness_step=0.1,
    animation_speed=1,
    rgb_order=(1, 0, 2),
)
keyboard.extensions.append(rgb)

# encoder setup - A on D8, B on D9
enc = EncoderHandler()
keyboard.modules.append(enc)
enc.pins = ((board.D8, board.D9, None,),)

# layer stuff
layers = Layers()
keyboard.modules.append(layers)

# oled if it works
if oled_working:
    disp = Display(
        display=SSD1306(
            i2c=board.I2C(),
            device_address=0x3C,
        ),
        width=128,
        height=32,
        flip=False,
    )
    
    # dynamic layer display
    class LayerDisplay(TextEntry):
        def __init__(self):
            super().__init__(text='L: 0', x=0, y=16)
        
        def update(self, keyboard):
            curr = keyboard.active_layers[0] if keyboard.active_layers else 0
            self.text = f'L: {curr}'
            return True
    
    disp.entries = [
        TextEntry(text='CodeMate', x=0, y=0),
        LayerDisplay(),
    ]
    keyboard.extensions.append(disp)
else:
    disp = None

# my custom macros
git_st = KC.MACRO("git status\n")
git_cm = KC.MACRO("git commit -m \"\"\n", delay=10)  # leaves cursor between quotes
git_ps = KC.MACRO("git push\n")

# vs code terminal shortcut
term = KC.LCTL(KC.LSFT(KC.GRAVE))
save = KC.LCTL(KC.S)

# these are useful for commenting code quickly
cblock = KC.MACRO("# =============================================\n# \n# =============================================\n")
todo = KC.MACRO("// TODO: ")

# windows shortcuts
ss = KC.LWIN(KC.LSFT(KC.S))  # screenshot
emoji = KC.LWIN(KC.DOT)  # emoji picker

# keymaps for the 3 layers i'm using
keyboard.keymap = [
    # layer 0 - default stuff
    # copy | paste | undo
    # cut  | save  | redo
    # L1   | L2    | screenshot
    [
        KC.LCTL(KC.C), KC.LCTL(KC.V), KC.LCTL(KC.Z),
        KC.LCTL(KC.X), save, KC.LCTL(KC.Y),
        KC.MO(1), KC.MO(2), ss,
    ],
    
    # layer 1 - coding mode
    # git status | terminal | F5 debug
    # git commit | comment  | todo
    # (hold)     | run py   | git push
    [
        git_st, term, KC.F5,
        git_cm, cblock, todo,
        KC.TRNS, KC.MACRO("python main.py\n"), git_ps,
    ],
    
    # layer 2 - media controls
    # play/pause | next | prev
    # mute       | vol+ | vol-
    # back       | hold | emoji
    [
        KC.MPLY, KC.MNXT, KC.MPRV,
        KC.MUTE, KC.VOLU, KC.VOLD,
        KC.WBAK, KC.TRNS, emoji,
    ],
]

# encoder does different things per layer
# layer 0: volume
# layer 1: zoom for when i'm coding
# layer 2: skip tracks
enc.map = [
    ((KC.VOLU, KC.VOLD),),
    ((KC.LCTL(KC.EQUAL), KC.LCTL(KC.MINUS)),),
    ((KC.MNXT, KC.MPRV),),
]

# layer colors - blue/green/purple
curr_layer = 0

def upd_rgb(keyboard):
    global curr_layer
    new_layer = keyboard.active_layers[0] if keyboard.active_layers else 0
    
    if new_layer != curr_layer:
        curr_layer = new_layer
        if curr_layer == 0:
            rgb.set_rgb_fill((0, 100, 255))
        elif curr_layer == 1:
            rgb.set_rgb_fill((0, 255, 100))
        elif curr_layer == 2:
            rgb.set_rgb_fill((200, 0, 255))

# hook into kmk's main loop
keyboard.before_hid_send = upd_rgb

# start with blue
rgb.set_rgb_fill((0, 100, 255))

if __name__ == '__main__':
    keyboard.go()