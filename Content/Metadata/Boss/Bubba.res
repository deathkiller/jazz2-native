{
    "Version": {
        "Target": "Jazz² Resurrection"
    },

    "Animations": {
        "Idle": {
            "Path": "Bubba/hop.png",
            "States": [ 0 ],
            "FrameCount": 1
        },
        "JumpStart": {
            "Path": "Bubba/hop.png",
            "States": [ 1073741825 ],
            "FrameCount": 4,
            "FrameRate": 11
        },
        "Jump": {
            "Path": "Bubba/hop.png",
            "States": [ 4 ],
            "Flags": 1,
            "FrameOffset": 4,
            "FrameCount": 3,
            "FrameRate": 13
        },
        "Fall": {
            "Path": "Bubba/jump.png",
            "States": [ 8 ],
            "Flags": 1,
            "FrameCount": 3,
            "FrameRate": 12
        },
        "FallEnd": {
            "Path": "Bubba/jump.png",
            "States": [ 1073741826 ],
            "FrameOffset": 3,
            "FrameCount": 6,
            "FrameRate": 14
        },
        "SpewFireball": {
            "Path": "Bubba/spew_fireball.png",
            "FrameCount": 12,
            "FrameRate": 12,
            "States": [ 16 ]
        },
        "SpewFireballEnd": {
            "Path": "Bubba/spew_fireball.png",
            "FrameOffset": 12,
            "FrameCount": 4,
            "FrameRate": 16,
            "States": [ 1073741828 ]
        },
        "Corpse": {
            "Path": "Bubba/corpse.png",
            "States": [ 1073741839 ],
            "FrameRate": 5
        },

        "TornadoStart": {
            "Path": "Bubba/tornado_start.png",
            "States": [ 1073741830 ]
        },
        "Tornado": {
            "Path": "Bubba/tornado.png",
            "States": [ 1073741831 ]
        },
        "TornadoEnd": {
            "Path": "Bubba/tornado_end.png",
            "States": [ 1073741832 ]
        },
        "TornadoFall": {
            "Path": "Bubba/jump_fall.png",
            "States": [ 1073741833 ]
        },

        "Fireball": {
            "Path": "Bubba/fireball.png",
            "FrameRate": 24,
            "States": [ 1073741834 ]
        }
    },

    "Sounds": {
        "Jump": {
            "Paths": [ "Bubba/hop_1.wav", "Bubba/hop_2.wav" ]
        },
        "Sneeze": {
            "Paths": [ "Bubba/sneeze.wav" ]
        }
    }
}