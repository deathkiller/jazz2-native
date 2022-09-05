{
	"Version": {
		"Target": "Jazz² Resurrection"
	},

	"Animations": {
		"Idle": {
			"Path": "TurtleToughBoss/idle.aura",
			"States": [ 0 ],
			"FrameRate": 8
		},
		"Walk": {
			"Path": "TurtleToughBoss/walk.aura",
			"States": [ 1 ],
			"FrameRate": 5
		},

		"AttackStart": {
			"Path": "TurtleToughBoss/attack_start.aura",
			"States": [ 1073741824 ],
			"FrameRate": 6,
			"FrameCount": 15
		},
		"AttackStart2": {
			"Path": "TurtleToughBoss/attack_start.aura",
			"States": [ 1073741825 ],
			"FrameRate": 10,
			"FrameOffset": 15,
			"FrameCount": 4
		},
		"AttackEnd": {
			"Path": "TurtleToughBoss/attack_end.aura",
			"States": [ 1073741826 ],
			"FrameRate": 7
		},
		
		"Mace": {
			"Path": "TurtleToughBoss/mace.aura",
			"States": [ 1073741827 ],
			"FrameRate": 12
		}
	},

	"Sounds": {
		"AttackStart": {
			"Paths": [ "TurtleToughBoss/attack_start.wav" ]
		},
		"AttackEnd": {
			"Paths": [ "TurtleToughBoss/attack_end.wav" ]
		},
		"Mace": {
			"Paths": [ "TurtleToughBoss/mace.wav" ]
		}
	}
}