# -*- coding: utf-8 -*-
import math
import numpy as np
import re
import time
import json


class Generic2048Control(object):

    def __init__(self, ctrl):
        self.ctrl = ctrl
        self.setup()

    def setup():
        raise NotImplementedError()

    def execute(self, cmd):
        return self.ctrl.execute(cmd)

    def get_status(self):
        ''' Check if the game is in an unusual state. '''
        return self.execute('''
            {Session.get('lost')}
           ''')

    def restart_game(self):
        self.send_key_event('keydown', 82)
        time.sleep(0.1)
        self.send_key_event('keyup', 82)

        self.send_key_event('keydown', 32)
        time.sleep(0.1)
        self.send_key_event('keyup', 32)

    def continue_game(self):
        ''' Continue the game. Only works if the game is in the 'won' state. '''
        self.execute('document.querySelector(".keep-playing-button").click();')

    def send_key_event(self, action, key):
        # Use generic events for compatibility with Chrome, which (for inexplicable reasons) doesn't support setting keyCode on KeyboardEvent objects.
        # See
        # http://stackoverflow.com/questions/8942678/keyboardevent-in-chrome-keycode-is-0.
        return self.execute('''
            var keyboardEvent = document.createEventObject ? document.createEventObject() : document.createEvent("Events");
            if(keyboardEvent.initEvent)
                keyboardEvent.initEvent("%(action)s", true, true);
            keyboardEvent.keyCode = %(key)s;
            keyboardEvent.which = %(key)s;
            var element = document.body || document;
            element.dispatchEvent ? element.dispatchEvent(keyboardEvent) : element.fireEvent("on%(action)s", keyboardEvent);
            ''' % locals())


class Fast2048Control(Generic2048Control):

    ''' Control 2048 by hooking the GameManager and executing its move() function.

    This is both safer and faster than the keyboard approach, but it is less compatible with clones. '''

    def setup(self):
        # Obtain the GameManager instance by triggering a fake restart.
        # self.ctrl.execute(
        #     '''
        #     var _func_tmp = GameManager.prototype.isGameTerminated;
        #     GameManager.prototype.isGameTerminated = function() {
        #         GameManager._instance = this;
        #         return true;
        #     };
        #     ''')

        # Send an "up" event, which will trigger our replaced isGameTerminated
        # function
        # self.send_key_event('keydown', 38)
        # time.sleep(0.1)
        # self.send_key_event('keyup', 38)

        # self.execute('GameManager.prototype.isGameTerminated = _func_tmp;')
        return

    def get_status(self):
        ''' Check if the game is in an unusual state. '''
        return self.execute('''
            Session.get('lost');
            ''')

    def get_score(self):
        return self.execute('GameManager._instance.score')

    def get_board(self):
        # Chrome refuses to serialize the Grid object directly through the
        # debugger.
        board = np.array(json.loads(self.execute('''JSON.stringify(Session.get('tiles'));''')))
        deck = json.loads(self.execute('''JSON.stringify(Session.get('current_deck'));'''))
        next_tile = int(self.execute('''Session.get('next_tile');'''))

        if next_tile > 3:
            # extra bonus tile
            max_possible = np.max(board) / 8
            tileset = []
            while max_possible > 3:
                tileset.append(max_possible)
                max_possible /= 2
        else:
            # restore to before drawing the tile
            deck.append(next_tile)
            tileset = [next_tile]

        # construct the dictionary deck from the array-format deck
        dictionary_deck = dict(zip([1,2,3], [0]*3))
        for tile in deck:
            dictionary_deck[tile] += 1
        return board, dictionary_deck, tileset

    def execute_move(self, move):
        # We use UDLR ordering;
        move = [38, 40, 37, 39][move]
        cmd = '''
        mock_event = {which: %(move)d};
        document.THREE.game.move(mock_event);
        ''' % locals()
        self.execute(cmd)

