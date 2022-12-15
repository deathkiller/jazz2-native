{
	"Version": {
		"Target": "Jazz² Resurrection"
	},

	"Animations": {
		"Idle": {
			"Path": "Devan/idle.aura",
			"States": [ 0 ]
		},
		"Run": {
			"Path": "Devan/run.aura",
			"States": [ 2 ]
		},
		"WarpIn": {
			"Path": "Devan/warp_in.aura",
			"States": [ 1073741843 ]
		},
		"WarpOut": {
			"Path": "Devan/warp_out.aura",
			"States": [ 1073741844 ]
		},
		"RunEnd": {
			"Path": "Devan/run_end.aura",
			"FrameRate": 20,
			"States": [ 1073741824 ]
		},
		"ShootStart": {
			"Path": "Devan/shoot.aura",
			"FrameCount": 3,
			"FrameRate": 20,
			"States": [ 15 ]
		},
		"Shoot": {
			"Path": "Devan/shoot.aura",
			"FrameOffset": 3,
			"FrameCount": 3,
			"FrameRate": 30,
			"States": [ 16 ]
		},
		"ShootEnd": {
			"Path": "Devan/shoot.aura",
			"FrameOffset": 6,
			"FrameCount": 1,
			"FrameRate": 40,
			"States": [ 17 ]
		},
		"ShootEnd2": {
			"Path": "Devan/shoot.aura",
			"FrameOffset": 7,
			"FrameRate": 28,
			"States": [ 18 ]
		},
		
		"JumpEnd": {
			"Path": "Devan/jump_end.aura",
			"States": [ 1073741826 ]
		},
		"Freefall": {
			"Path": "Devan/freefall.aura",
			"FrameRate": 30,
			"States": [ 65536 ]
		},

		"DisorientedStart": {
			"Path": "Devan/disoriented_start.aura",
			"FrameRate": 30,
			"States": [ 666 ]
		},
		"Disoriented": {
			"Path": "Devan/disoriented.aura",
			"States": [ 667 ]
		},
		"DisorientedWarpOut": {
			"Path": "Devan/disoriented_warp_out.aura",
			"States": [ 6670 ]
		},

		"Bullet": {
			"Path": "Devan/bullet.aura",
			"States": [ 668 ]
		},
		
		"DemonFly": {
			"Path": "Devan/demon_fly.aura",
			"FrameRate": 5,
			"States": [ 669 ]
		},
		"DemonTransformStart": {
			"Path": "Devan/demon_transform_start.aura",
			"FrameRate": 2,
			"States": [ 670 ]
		},
		"DemonTransformEnd": {
			"Path": "Devan/demon_transform_end.aura",
			"FrameRate": 4,
			"States": [ 671 ]
		},
		"DemonTurn": {
			"Path": "Devan/demon_turn.aura",
			"FrameRate": 20,
			"States": [ 1073741840 ]
		},
		"DemonSpewFireball": {
			"Path": "Devan/demon_spew_fireball.aura",
			"FrameCount": 9,
			"States": [ 673 ]
		},
		"DemonSpewFireballEnd": {
			"Path": "Devan/demon_spew_fireball.aura",
			"FrameOffset": 9,
			"FrameRate": 20,
			"States": [ 674 ]
		},
		
		"DemonFireball": {
			"Path": "Devan/demon_fireball.aura",
			"States": [ 675 ]
		}
	},

	"Sounds": {
		"Shoot": {
			"Paths": [ "Devan/shoot.wav" ]
		},
		"SpitFireball": {
			"Paths": [ "Devan/spit_fireball.wav" ]
		},
		"Flap": {
			"Paths": [ "Devan/flap.wav" ]
		},
		"WallPoof": {
			"Paths": [ "Weapon/wall_poof.wav" ]
		}
	}
}