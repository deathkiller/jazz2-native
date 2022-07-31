{
    "Version": {
        "Target": "Jazz² Resurrection"
    },

    "Animations": {
        "Idle": {
            "Path": "Spaz/idle.png",
            "States": [ 0 ]
        },
        "Walk": {
            "Path": "Spaz/run.png",
            "States": [ 1, 17 ]
        },
        "Run": {
            "Path": "Spaz/dash_start.png",
            "States": [ 2, 18 ]
        },
        "Jump": {
            "Path": "Spaz/jump.png",
            "States": [ 4 ]
        },
        "RunJump": {
            "Path": "Spaz/jump_diag.png",
            "Flags": 1,
            "States": [ 5, 6 ]
        },
        "Fall": {
            "Path": "Spaz/fall.png",
            "States": [ 8 ]
        },
        "FallDiag": {
            "Path": "Spaz/fall_diag.png",
            "States": [ 9, 10, 11 ]
        },
        "Freefall": {
            "Path": "Spaz/freefall.png",
            "States": [ 65536, 65537, 65538, 65539 ]
        },
        "Dash": {
            "Path": "Spaz/dash.png",
            "FrameRate": 20,
            "States": [ 3, 19 ]
        },
        "DashJump": {
            "Path": "Spaz/ball.png",
            "States": [ 7 ]
        },
        "Lookup": {
            "Path": "Spaz/lookup_start.png",
            "Flags": 1,
            "FrameRate": 34,
            "States": [ 64 ]
        },
        "Crouch": {
            "Path": "Spaz/crouch_start.png",
            "Flags": 1,
            "FrameRate": 38,
            "States": [ 32 ]
        },
        "DizzyIdle": {
            "Path": "Spaz/dizzy.png",
            "FrameRate": 7,
            "States": [ 128 ]
        },
        "DizzyWalk": {
            "Path": "Spaz/dizzy_walk.png",
            "FrameRate": 6,
            "States": [ 129, 145 ]
        },
        "Shoot": {
            "Path": "Spaz/shoot.png",
            "States": [ 16, 144 ],
            "Flags": 1
        },
        "CrouchShoot": {
            "Path": "Spaz/crouch_shoot.png",
            "States": [ 48 ],
            "Flags": 1
        },
        "LookupShoot": {
            "Path": "Spaz/shoot_ver.png",
            "States": [ 80 ],
            "Flags": 1
        },
        "Hurt": {
            "Path": "Spaz/hurt.png",
            "FrameRate": 6,
            "States": [ 2048 ]
        },
        "Sidekick": {
            "Path": "Spaz/sidekick.png",
            "States": [ 512 ]
        },
        "Buttstomp": {
            "Path": "Spaz/buttstomp.png",
            "States": [ 256 ]
        },
        "Hook": {
            "Path": "Spaz/vine_idle.png",
            "States": [ 12 ]
        },
        "HookLookup": {
            "Path": "Spaz/vine_shoot_up_end.png",
            "States": [ 76 ]
        },
        "HookMove": {
            "Path": "Spaz/vine_walk.png",
            "States": [ 13, 14, 15 ]
        },
        "Copter": {
            "Path": "Spaz/copter.png",
            "FrameRate": 8,
            "States": [ 8192, 8193, 8194, 8195 ]
        },
        "CopterShoot": {
            "Path": "Spaz/copter_shoot.png",
            "States": [ 8208, 8209, 8210, 8211 ]
        },
        "AerialShoot": {
            "Path": "Spaz/fall_shoot.png",
            "FrameRate": 14,
            "States": [ 20, 21, 22, 23, 24, 25, 26, 27, 262160, 262161, 262162, 262163 ]
        },
        "HookShoot": {
            "Path": "Spaz/vine_shoot.png",
            "States": [ 28, 29, 30, 31 ],
            "Flags": 1
        },
        "HookLookupShoot": {
            "Path": "Spaz/vine_shoot_up.png",
            "States": [ 92 ],
            "Flags": 1
        },
        "RunToIdle": {
            "Path": "Spaz/run_stop.png",
            "States": [ 1073741824 ]
        },
        "RunToDash": {
            "Path": "Spaz/dash_start.png",
            "States": [ 1073741825 ]
        },
        "DashToIdle": {
            "Path": "Spaz/dash_stop.png",
            "States": [ 1073741856 ]
        },
        "FallToIdle": {
            "Path": "Spaz/fall_end.png",
            "States": [ 1073741826 ]
        },
        "ShootToIdle": {
            "Path": "Spaz/shoot_start.png",
            "FrameRate": 20,
            "States": [ 1073741828 ]
        },
        "HookShootToHook": {
            "Path": "Spaz/vine_shoot_end.png",
            "FrameRate": 24,
            "States": [ 1073741829 ]
        },
        "CopterShootToCopter": {
            "Path": "Spaz/copter_shoot_start.png",
            "FrameRate": 24,
            "States": [ 1073741830 ]
        },
        "FallShootToFall": {
            "Path": "Spaz/unused_jump_shoot_end.png",
            "FrameRate": 20,
            "States": [ 1073741872 ]
        },
        "SidekickA": {
            "Path": "Spaz/sidekick_start.png",
            "FrameCount": 5,
            "FrameRate": 20,
            "States": [ 1073741831 ]
        },
        "SidekickB": {
            "Path": "Spaz/sidekick_start.png",
            "FrameOffset": 5,
            "FrameRate": 20,
            "States": [ 1073741832 ]
        },
        "SidekickC": {
            "Path": "Spaz/sidekick_end.png",
            "FrameRate": 30,
            "States": [ 1073741833 ]
        },
        "ButtstompStart": {
            "Path": "Spaz/Spring.png",
            "States": [ 1073741834 ]
        },
        "ButtstompEnd": {
            "Path": "Spaz/buttstomp_end.png",
            "States": [ 1073741858 ]
        },
        "PoleH": {
            "Path": "Spaz/pole_h.png",
            "FrameRate": 20,
            "States": [ 1073741835 ]
        },
        "PoleHSlow": {
            "Path": "Spaz/pole_h.png",
            "States": [ 1073741837 ]
        },
        "PoleV": {
            "Path": "Spaz/pole_v.png",
            "FrameRate": 20,
            "States": [ 1073741836 ]
        },
        "PoleVSlow": {
            "Path": "Spaz/pole_v.png",
            "States": [ 1073741838 ]
        },
        "Death": {
            "Path": "Spaz/die.png",
            "FrameRate": 3,
            "States": [ 1073741839 ]
        },
        "WarpIn": {
            "Path": "Spaz/warp_in.png",
            "States": [ 1073741843 ]
        },
        "WarpOut": {
            "Path": "Spaz/warp_out.png",
            "States": [ 1073741844 ]
        },
        "WarpInFreefall": {
            "Path": "Spaz/warp_in_freefall.png",
            "States": [ 1073741847 ]
        },
        "WarpOutFreefall": {
            "Path": "Spaz/warp_out_freefall.png",
            "States": [ 1073741848 ]
        },
        "Spring": {
            "Path": "Spaz/Spring.png",
            "States": [ 262144, 262145, 262146, 262147 ]
        },
        "Push": {
            "Path": "Spaz/push.png",
            "FrameRate": 7,
            "States": [ 16384, 16385 ]
        },
        "EndOfLevel": {
            "Path": "Spaz/eol.png",
            "FrameRate": 3,
            "States": [ 1073741846 ]
        },
        "Swim": {
            "Path": "Spaz/swim_right.png",
            "FrameRate": 6,
            "States": [ 4096 ]
        },
        "Lift": {
            "Path": "Spaz/lift.png",
            "Flags": 1,
            "FrameRate": 16,
            "States": [ 131072 ]
        },
        "LiftStart": {
            "Path": "Spaz/lift_jump_heavy.png",
            "FrameRate": 16,
            "States": [ 1073741859 ]
        },
        "LiftEnd": {
            "Path": "Spaz/lift_jump_light.png",
            "States": [ 1073741860 ]
        },
        "Ledge": {
            "Path": "Spaz/ledge.png",
            "FrameRate": 3,
            "States": [ 1073741861 ]
        },
        "Airboard": {
            "Path": "Spaz/airboard.png",
            "FrameRate": 6,
            "States": [ 1024 ]
        },
        "LedgeClimb": {
            "Path": "Spaz/unused_ledge_climb.png",
            "FrameRate": 8,
            "States": [ 1073741862 ]
        },
        "Swing": {
            "Path": "Spaz/swing.png",
            "FrameRate": 4,
            "States": [ 32768 ],
            "Flags": 1
        },
        
        "IdleBored1": {
            "Path": "Spaz/idle_flavor_2.png",
            "FrameRate": 2,
            "States": [ 1073741849 ]
        },
        "IdleBored2": {
            "Path": "Spaz/idle_flavor_3.png",
            "FrameRate": 1,
            "States": [ 1073741849 ]
        },
        "IdleBored3": {
            "Path": "Spaz/idle_flavor_4.png",
            "FrameRate": 3,
            "States": [ 1073741849 ]
        },
        "IdleBored4": {
            "Path": "Spaz/idle_flavor_5.png",
            "FrameRate": 3,
            "States": [ 1073741849 ]
        },

        "TransformFromFrog": {
            "Path": "Spaz/transform_frog_end.png",
            "States": [ 1073741888 ],
            "FrameRate": 3
        },
        
        "Corpse": {
            "Path": "Spaz/corpse.png"
        },

        "SugarRush": {
            "Path": "Common/SugarRushStars.png"
        }
    },

    "Sounds": {
        "SwitchAmmo": {
            "Paths": [ "UI/weapon_change.wav" ]
        },
        "EndOfLevel": {
            "Paths": [ "Spaz/level_complete.wav" ]
        },
        "WarpIn": {
            "Paths": [ "Common/warp_in.wav" ]
        },
        "WarpOut": {
            "Paths": [ "Common/warp_out.wav" ]
        },
        "Jump": {
            "Paths": [ "Common/char_jump.wav" ]
        },
        "Land": {
            "Paths": [ "Common/char_land.wav" ]
        },
        "Hurt": {
            "Paths": [ "Spaz/hurt_1.wav", "Spaz/hurt_2.wav" ]
        },
        "Die": {
            "Paths": [ "Spaz/idle_flavor_4.wav" ]
        },
        "Copter": {
            "Paths": [ "Common/copter_noise.wav" ]
        },
        "PickupAmmo": {
            "Paths": [ "Pickup/ammo.wav" ]
        },
        "PickupCoin": {
            "Paths": [ "Pickup/coin.wav" ]
        },
        "PickupGem": {
            "Paths": [ "Pickup/gem.wav" ]
        },
        "PickupOneUp": {
            "Paths": [ "Pickup/1up.wav" ]
        },
        "PickupDrink": {
            "Paths": [ "Pickup/food_drink_1.wav", "Pickup/food_drink_2.wav", "Pickup/food_drink_3.wav", "Pickup/food_drink_4.wav" ]
        },
        "PickupFood": {
            "Paths": [ "Pickup/food_edible_1.wav", "Pickup/food_edible_2.wav", "Pickup/food_edible_3.wav", "Pickup/food_edible_4.wav" ]
        },
        "PickupMaxCarrot": {
            "Paths": [ "Pickup/food_edible_1.wav" ]
        },
        "WeaponBlaster": {
            "__Paths": [ "Weapon/bullet_blaster_spaz_1.wav", "Weapon/bullet_blaster_spaz_2.wav", "Weapon/bullet_blaster_spaz_3.wav" ],
            "Paths": [ "Weapon/bullet_blaster_jazz_2.wav", "Weapon/bullet_blaster_jazz_3.wav" ]
        },
        "WeaponToaster": {
            "Paths": [ "Weapon/toaster.wav" ]
        },
        "HookAttach": {
            "Paths": [ "Common/swish_9.wav" ]
        },
        "Buttstomp": {
            "Paths": [ "Common/down.wav" ]
        },
        "Buttstomp2": {
            "Paths": [ "Spaz/jump_3.wav" ]
        },
        "EndOfLevel1": {
            "Paths": [ "Common/char_revup.wav" ]
        },
        "EndOfLevel2": {
            "Paths": [ "Weapon/ricochet_bullet_3.wav" ]
        },
        "Spring": {
            "Paths": [ "Spaz/spring_1.wav", "Spaz/spring_2.wav" ]
        },
        "Pole": {
            "Paths": [ "Birdy/fly_1.wav", "Birdy/fly_2.wav" ]
        },
        "Ledge": {
            "Paths": [ "Spaz/ledge.wav" ]
        },
        "DoubleJump": {
            "Paths": [ "Common/char_double_jump.wav" ]
        },
        "Sidekick": {
            "Paths": [ "Spaz/sidekick_1.wav", "Spaz/sidekick_2.wav" ]
        },
        "BonusWarpNotEnoughCoins": {
            "Paths": [ "Object/BonusNotEnoughCoins.wav" ]
        }
    }
}