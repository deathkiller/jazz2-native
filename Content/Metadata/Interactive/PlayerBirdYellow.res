{
	"Version": {
		"Target": "Jazz² Resurrection"
	},

	"Animations": {
		"Fly": {
			"Path": "BirdyYellow/fly.aura",
			"States": [ 1, 2, 3 ],
			"FrameRate": 7
		},
		"Idle": {
			"Path": "BirdyYellow/idle.aura",
			"States": [ 0 ]
		},
		"Attack": {
			"Path": "BirdyYellow/fly.aura",
			"States": [ 16, 144 ],
  			"Flags": 1
		},
		"Jump": {
			"Path": "BirdyYellow/charge_ver.aura",
			"States": [ 4 ]
		},
		"Hurt": {
			"Path": "BirdyYellow/hurt.aura",
			"FrameRate": 6,
			"States": [ 2048 ]
		},
		"Death": {
			"Path": "BirdyYellow/die.aura",
			"FrameRate": 3,
			"States": [ 1073741839 ]
		},
		"Corpse": {
			"Path": "BirdyYellow/corpse.aura",
			"States": [ 536870912 ]
		},
		"WarpIn": {
			"Path": "Spaz/warp_in.aura",
			"States": [ 1073741843 ],
			"FrameOffset": 2,
		},
		"WarpOut": {
			"Path": "Spaz/warp_out.aura",
			"States": [ 1073741844 ],
			"FrameCount": 5,
		},
		"WarpInFreefall": {
			"Path": "Spaz/warp_in.aura",
			"States": [ 1073741843 ],
			"FrameOffset": 2,
		},
		"WarpOutFreefall": {
			"Path": "Spaz/warp_out.aura",
			"States": [ 1073741844 ],
			"FrameCount": 5,
		}
	},

	"Sounds": {
		"Fly": {
				"Paths": [ "Birdy/fly_1.wav", "Birdy/fly_2.wav" ]
			},
		"WarpIn": {
			"Paths": [ "Common/warp_in.wav" ]
		},
		"WarpOut": {
			"Paths": [ "Common/warp_out.wav" ]
		},
		"WeaponBlaster": {
			"Paths": [ "Weapon/bullet_blaster_jazz_4.wav" ]
		}
	}
}