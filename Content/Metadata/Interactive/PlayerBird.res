{
	"Version": {
		"Target": "Jazz² Resurrection"
	},

	"Animations": {
		"Fly": {
			"Path": "Birdy/fly.aura",
			"States": [ 1, 2, 3 ],
			"FrameRate": 7
		},
		"Idle": {
			"Path": "Birdy/idle.aura",
			"States": [ 0 ]
		},
		"Attack": {
			"Path": "Birdy/fly.aura",
			"States": [ 16, 144 ],
			"Flags": 1
		},
		"Jump": {
			"Path": "Birdy/charge_ver.aura",
			"States": [ 4 ]
		},
		"Hurt": {
			"Path": "Birdy/hurt.aura",
			"FrameRate": 6,
			"States": [ 2048 ]
		},
		"Death": {
			"Path": "Birdy/die.aura",
			"FrameRate": 3,
			"States": [ 1073741839 ]
		},
		"Corpse": {
			"Path": "Birdy/corpse.aura",
			"States": [ 536870912 ]
		}
	},

	"Sounds": {
		"Fly": {
			"Paths": [ "Birdy/fly_1.wav", "Birdy/fly_2.wav" ]
		},
		"WeaponBlaster": {
			"Paths": [ "Weapon/bullet_blaster_jazz_4.wav" ]
		}
	}
}
