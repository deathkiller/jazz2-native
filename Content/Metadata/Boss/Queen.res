{
    "Version": {
        "Target": "Jazz² Resurrection"
    },
    
    "BoundingBox": [ 20, 30 ],

    "Animations": {
        "Idle": {
            "Path": "Queen/idle.png",
            "States": [ 0 ],
            "FrameRate": 6
        },
        "Fall": {
            "Path": "Queen/fall.png",
            "States": [ 8 ],
            "FrameRate": 6
        },

        "Scream": {
            "Path": "Queen/scream.png",
            "States": [ 1073741824 ],
            "FrameRate": 3
        },
        "Stomp": {
            "Path": "Queen/stomp.png",
            "FrameRate": 10,
            "FrameCount": 6,
            "States": [ 1073741825 ]
        },
        "StompEnd": {
            "Path": "Queen/stomp.png",
            "FrameRate": 20,
            "FrameOffset": 6,
            "States": [ 1073741830 ]
        },
        "Backstep": {
            "Path": "Queen/backstep.png",
            "States": [ 1073741826 ],
            "FrameRate": 8
        },
        "Ledge": {
            "Path": "Queen/ledge.png",
            "States": [ 1073741827 ],
            "FrameRate": 4
        },
        "LedgeRecover": {
            "Path": "Queen/ledge_recover.png",
            "States": [ 1073741828 ],
            "FrameRate": 6
        },
        "Brick": {
            "Path": "Queen/brick.png",
            "States": [ 1073741829 ]
        }
    },

    "Sounds": {
        "Scream": {
            "Paths": [ "Queen/scream.wav" ]
        },
        "Spring": {
            "Paths": [ "Queen/Spring.wav" ]
        },
        "Stomp": {
            "Paths": [ "Common/gunsm1.wav" ]
        },
        "BrickFalling": {
            "Paths": [ "Common/holyflut.wav" ]
        }
    }
}