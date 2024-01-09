#include "AnimSetMapping.h"

namespace Jazz2::Compatibility
{
	AnimSetMapping::AnimSetMapping(JJ2Version version)
		: _version(version), _currentItem(0), _currentSet(0), _currentOrdinal(0)
	{
	}

	AnimSetMapping AnimSetMapping::GetAnimMapping(JJ2Version version)
	{
		AnimSetMapping m(version);

		if (version == JJ2Version::PlusExtension) {
			m.SkipItems(5); // Unimplemented weapon
			m.Add("Pickup"_s, "fast_fire_lori"_s);
			m.Add("UI"_s, "blaster_upgraded_lori"_s);

			m.NextSet();
			m.DiscardItems(4); // Beta version sprites

			m.NextSet();
			m.Add("Object"_s, "crate_ammo_pepper"_s);
			m.Add("Object"_s, "crate_ammo_electro"_s);
			m.Add("Object"_s, "powerup_shield_laser"_s);
			m.Add("Object"_s, "powerup_unknown"_s);
			m.Add("Object"_s, "powerup_empty"_s);
			m.Add("Object"_s, "powerup_upgrade_blaster_lori"_s);
			m.Add("Common"_s, "SugarRushStars"_s);
			m.SkipItems(); // Carrotade

			m.NextSet(); // 3
			m.DiscardItems(3); // Lori's continue animations

			m.NextSet(); // 4

			//m.Add("UI"_s, "font_medium"_s);
			//m.Add("UI"_s, "font_small"_s);
			//m.Add("UI"_s, "font_large"_s);
			m.DiscardItems(3);

			//m.Add("UI"_s, "logo_plus"_s, skipNormalMap: true);
			m.DiscardItems(1);

			m.NextSet(); // 5
			m.Add("Object"_s, "powerup_swap_characters_lori"_s);

			//m.Add("UI"_s, "logo_plus_large"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			//m.Add("UI"_s, "logo_plus_small"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(2);

			m.NextSet(); // 6
			m.DiscardItems(5); // Reticles

		} else if (version != JJ2Version::Unknown) {
			bool isFull = ((version & JJ2Version::SharewareDemo) != JJ2Version::SharewareDemo);

			// set 0 (all)
			m.Add("Unknown"_s, "flame_blue"_s);
			m.Add("Common"_s, "bomb"_s);
			m.Add("Common"_s, "smoke_poof"_s);
			m.Add("Common"_s, "explosion_rf"_s);
			m.Add("Common"_s, "explosion_small"_s);
			m.Add("Common"_s, "explosion_large"_s);
			m.Add("Common"_s, "smoke_circling_gray"_s);
			m.Add("Common"_s, "smoke_circling_brown"_s);
			m.Add("Weapon"_s, "shield_water"_s);

			//m.Add("Unknown"_s, "brown_thing"_s);
			m.DiscardItems(1);

			m.Add("Common"_s, "explosion_pepper"_s);
			m.Add("Weapon"_s, "shield_lightning"_s);
			m.Add("Weapon"_s, "shield_lightning_trail"_s);
			m.Add("Unknown"_s, "flame_red"_s);
			m.Add("Weapon"_s, "shield_fire"_s);
			m.Add("Weapon"_s, "flare_diag_downleft"_s);
			m.Add("Weapon"_s, "flare_hor"_s);
			m.Add("Weapon"_s, "bullet_blaster"_s);
			m.Add("UI"_s, "blaster_upgraded_jazz"_s);
			m.Add("UI"_s, "blaster_upgraded_spaz"_s);
			m.Add("Weapon"_s, "bullet_blaster_upgraded"_s);

			//m.Add("Weapon"_s, "bullet_blaster_upgraded_ver"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_blaster_ver"_s);
			m.DiscardItems(1);

			m.Add("Weapon"_s, "bullet_bouncer"_s);
			m.Add("Pickup"_s, "ammo_bouncer_upgraded"_s);
			m.Add("Pickup"_s, "ammo_bouncer"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded"_s);
			m.Add("Weapon"_s, "bullet_freezer_hor"_s);
			m.Add("Pickup"_s, "ammo_freezer_upgraded"_s);
			m.Add("Pickup"_s, "ammo_freezer"_s);
			m.Add("Weapon"_s, "bullet_freezer_upgraded_hor"_s);

			//m.Add("Weapon"_s, "bullet_freezer_ver"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_freezer_upgraded_ver"_s);
			m.DiscardItems(1);

			m.Add("Pickup"_s, "ammo_seeker_upgraded"_s);
			m.Add("Pickup"_s, "ammo_seeker"_s);

			//m.Add("Weapon"_s, "bullet_seeker_ver_down"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_seeker_diag_downright"_s);
			m.DiscardItems(1);

			m.Add("Weapon"_s, "bullet_seeker_hor"_s);

			//m.Add("Weapon"_s, "bullet_seeker_ver_up"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_seeker_diag_upright"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_seeker_upgraded_ver_down"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_seeker_upgraded_diag_downright"_s);
			m.DiscardItems(1);

			m.Add("Weapon"_s, "bullet_seeker_upgraded_hor"_s);

			//m.Add("Weapon"_s, "bullet_seeker_upgraded_ver_up"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_seeker_upgraded_diag_upright"_s);
			m.DiscardItems(1);

			m.Add("Weapon"_s, "bullet_rf_hor"_s);

			//m.Add("Weapon"_s, "bullet_rf_diag_downright"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_rf_upgraded_diag_downright"_s);
			m.DiscardItems(1);

			m.Add("Pickup"_s, "ammo_rf_upgraded"_s);
			m.Add("Pickup"_s, "ammo_rf"_s);
			m.Add("Weapon"_s, "bullet_rf_upgraded_hor"_s);

			//m.Add("Weapon"_s, "bullet_rf_upgraded_ver"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_rf_upgraded_diag_upright"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_rf_ver"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_rf_diag_upright"_s);
			m.DiscardItems(1);

			m.Add("Weapon"_s, "bullet_toaster"_s);
			m.Add("Pickup"_s, "ammo_toaster_upgraded"_s);
			m.Add("Pickup"_s, "ammo_toaster"_s);
			m.Add("Weapon"_s, "bullet_toaster_upgraded"_s);
			m.Add("Weapon"_s, "bullet_tnt"_s);
			m.Add("Weapon"_s, "bullet_fireball_hor"_s);
			m.Add("Pickup"_s, "ammo_pepper_upgraded"_s);
			m.Add("Pickup"_s, "ammo_pepper"_s);
			m.Add("Weapon"_s, "bullet_fireball_upgraded_hor"_s);

			//m.Add("Weapon"_s, "bullet_fireball_ver"_s);
			m.DiscardItems(1);
			//m.Add("Weapon"_s, "bullet_fireball_upgraded_ver"_s);
			m.DiscardItems(1);

			m.Add("Weapon"_s, "bullet_bladegun"_s);
			m.Add("Pickup"_s, "ammo_electro_upgraded"_s);
			m.Add("Pickup"_s, "ammo_electro"_s);
			m.Add("Weapon"_s, "bullet_bladegun_upgraded"_s);
			m.Add("Common"_s, "explosion_tiny"_s);
			m.Add("Common"_s, "explosion_freezer_maybe"_s);
			m.Add("Common"_s, "explosion_tiny_black"_s);
			m.Add("Weapon"_s, "bullet_fireball_upgraded_hor_2"_s);
			m.Add("Weapon"_s, "flare_hor_2"_s);
			m.Add("Unknown"_s, "green_explosion"_s);
			m.Add("Weapon"_s, "bullet_bladegun_alt"_s);
			m.Add("Weapon"_s, "bullet_tnt_explosion"_s);
			m.Add("Object"_s, "container_ammo_shrapnel_1"_s);
			m.Add("Object"_s, "container_ammo_shrapnel_2"_s);
			m.Add("Common"_s, "explosion_upwards"_s);
			m.Add("Common"_s, "explosion_bomb"_s);
			m.Add("Common"_s, "smoke_circling_white"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Bat"_s, "idle"_s);
				m.Add("Bat"_s, "resting"_s);
				m.Add("Bat"_s, "takeoff_1"_s);
				m.Add("Bat"_s, "takeoff_2"_s);
				m.Add("Bat"_s, "roost"_s);
				m.NextSet();
				m.Add("Bee"_s, "swarm"_s);
				m.NextSet();
				m.Add("Bee"_s, "swarm_2"_s);
				m.NextSet();
				m.Add("Object"_s, "pushbox_crate"_s);
				m.NextSet();
				m.Add("Object"_s, "pushbox_rock"_s);
				m.NextSet();

				//m.Add("Unknown"_s, "diamondus_tileset_tree"_s);
				m.DiscardItems(1);

				m.NextSet();
				m.Add("Bilsy"_s, "throw_fireball"_s);
				m.Add("Bilsy"_s, "appear"_s);
				m.Add("Bilsy"_s, "vanish"_s);
				m.Add("Bilsy"_s, "bullet_fireball"_s);
				m.Add("Bilsy"_s, "idle"_s);
			}

			m.NextSet();
			m.Add("Birdy"_s, "charge_diag_downright"_s);
			m.Add("Birdy"_s, "charge_ver"_s);
			m.Add("Birdy"_s, "charge_diag_upright"_s);
			m.Add("Birdy"_s, "caged"_s);
			m.Add("Birdy"_s, "cage_destroyed"_s);
			m.Add("Birdy"_s, "die"_s);
			m.Add("Birdy"_s, "feather_green"_s);
			m.Add("Birdy"_s, "feather_red"_s);
			m.Add("Birdy"_s, "feather_green_and_red"_s);
			m.Add("Birdy"_s, "fly"_s);
			m.Add("Birdy"_s, "hurt"_s);
			m.Add("Birdy"_s, "idle_worm"_s);
			m.Add("Birdy"_s, "idle_turn_head_left"_s);
			m.Add("Birdy"_s, "idle_look_left"_s);
			m.Add("Birdy"_s, "idle_turn_head_left_back"_s);
			m.Add("Birdy"_s, "idle_turn_head_right"_s);
			m.Add("Birdy"_s, "idle_look_right"_s);
			m.Add("Birdy"_s, "idle_turn_head_right_back"_s);
			m.Add("Birdy"_s, "idle"_s);
			m.Add("Birdy"_s, "corpse"_s);

			if (isFull) {
				m.NextSet();
				//m.Add("Unimplemented"_s, "bonus_birdy"_s);	// Unused
				m.DiscardItems(1);
				m.NextSet(); // set 10 (all)
				m.Add("Platform"_s, "ball"_s);
				m.Add("Platform"_s, "ball_chain"_s);
				m.NextSet();
				m.Add("Object"_s, "bonus_active"_s);
				m.Add("Object"_s, "bonus_inactive"_s);	// Unused
			}

			m.NextSet();
			m.Add("UI"_s, "boss_health_bar"_s, JJ2DefaultPalette::Sprite, true);
			m.NextSet();
			m.Add("Bridge"_s, "rope"_s);
			m.Add("Bridge"_s, "stone"_s);
			m.Add("Bridge"_s, "vine"_s);
			m.Add("Bridge"_s, "stone_red"_s);
			m.Add("Bridge"_s, "log"_s);
			m.Add("Bridge"_s, "gem"_s);
			m.Add("Bridge"_s, "lab"_s);
			m.NextSet();
			m.Add("Bubba"_s, "spew_fireball"_s);
			m.Add("Bubba"_s, "corpse"_s);
			m.Add("Bubba"_s, "jump"_s);
			m.Add("Bubba"_s, "jump_fall"_s);
			m.Add("Bubba"_s, "fireball"_s);
			m.Add("Bubba"_s, "hop"_s);
			m.Add("Bubba"_s, "tornado"_s);
			m.Add("Bubba"_s, "tornado_start"_s);
			m.Add("Bubba"_s, "tornado_end"_s);
			m.NextSet();
			m.Add("Bee"_s, "bee"_s);
			m.Add("Bee"_s, "bee_turn"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Unimplemented"_s, "butterfly"_s);
				m.NextSet();
				m.Add("Pole"_s, "carrotus"_s);
				m.NextSet();
				m.Add("Cheshire"_s, "platform_appear"_s);
				m.Add("Cheshire"_s, "platform_vanish"_s);
				m.Add("Cheshire"_s, "platform_idle"_s);
				m.Add("Cheshire"_s, "platform_invisible"_s);
			}

			m.NextSet();
			m.Add("Cheshire"_s, "hook_appear"_s);
			m.Add("Cheshire"_s, "hook_vanish"_s);
			m.Add("Cheshire"_s, "hook_idle"_s);
			m.Add("Cheshire"_s, "hook_invisible"_s);

			m.NextSet(); // set 20 (all)
			m.Add("Caterpillar"_s, "exhale_start"_s);
			m.Add("Caterpillar"_s, "exhale"_s);
			m.Add("Caterpillar"_s, "disoriented"_s);
			m.Add("Caterpillar"_s, "idle"_s);
			m.Add("Caterpillar"_s, "inhale_start"_s);
			m.Add("Caterpillar"_s, "inhale"_s);
			m.Add("Caterpillar"_s, "smoke"_s);

			if (isFull) {
				m.NextSet();
				m.Add("BirdyYellow"_s, "charge_diag_downright_placeholder"_s);
				m.Add("BirdyYellow"_s, "charge_ver"_s);
				m.Add("BirdyYellow"_s, "charge_diag_upright"_s);
				m.Add("BirdyYellow"_s, "caged"_s);
				m.Add("BirdyYellow"_s, "cage_destroyed"_s);
				m.Add("BirdyYellow"_s, "die"_s);
				m.Add("BirdyYellow"_s, "feather_blue"_s);
				m.Add("BirdyYellow"_s, "feather_yellow"_s);
				m.Add("BirdyYellow"_s, "feather_blue_and_yellow"_s);
				m.Add("BirdyYellow"_s, "fly"_s);
				m.Add("BirdyYellow"_s, "hurt"_s);
				m.Add("BirdyYellow"_s, "idle_worm"_s);
				m.Add("BirdyYellow"_s, "idle_turn_head_left"_s);
				m.Add("BirdyYellow"_s, "idle_look_left"_s);
				m.Add("BirdyYellow"_s, "idle_turn_head_left_back"_s);
				m.Add("BirdyYellow"_s, "idle_turn_head_right"_s);
				m.Add("BirdyYellow"_s, "idle_look_right"_s);
				m.Add("BirdyYellow"_s, "idle_turn_head_right_back"_s);
				m.Add("BirdyYellow"_s, "idle"_s);
				m.Add("BirdyYellow"_s, "corpse"_s);
			}

			m.NextSet();
			m.Add("Common"_s, "water_bubble_1"_s);
			m.Add("Common"_s, "water_bubble_2"_s);
			m.Add("Common"_s, "water_bubble_3"_s);
			m.Add("Common"_s, "water_splash"_s);

			m.NextSet();
			m.Add("Jazz"_s, "gameover_continue"_s);
			m.Add("Jazz"_s, "gameover_idle"_s);
			m.Add("Jazz"_s, "gameover_end"_s);
			m.Add("Spaz"_s, "gameover_continue"_s);
			m.Add("Spaz"_s, "gameover_idle"_s);
			m.Add("Spaz"_s, "gameover_end"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Demon"_s, "idle"_s);
				m.Add("Demon"_s, "attack_start"_s);
				m.Add("Demon"_s, "attack"_s);
				m.Add("Demon"_s, "attack_end"_s);
			}

			m.NextSet();
			m.DiscardItems(4); // Green rectangles
			m.Add("Common"_s, "ice_block"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Devan"_s, "bullet_small"_s);
				m.Add("Devan"_s, "remote_idle"_s);
				m.Add("Devan"_s, "remote_fall_warp_out"_s);
				m.Add("Devan"_s, "remote_fall"_s);
				m.Add("Devan"_s, "remote_fall_rotate"_s);
				m.Add("Devan"_s, "remote_fall_warp_in"_s);
				m.Add("Devan"_s, "remote_warp_out"_s);

				m.NextSet();
				m.Add("Devan"_s, "demon_spew_fireball"_s);
				m.Add("Devan"_s, "disoriented"_s);
				m.Add("Devan"_s, "freefall"_s);
				m.Add("Devan"_s, "disoriented_start"_s);
				m.Add("Devan"_s, "demon_fireball"_s);
				m.Add("Devan"_s, "demon_fly"_s);
				m.Add("Devan"_s, "demon_transform_start"_s);
				m.Add("Devan"_s, "demon_transform_end"_s);
				m.Add("Devan"_s, "disarmed_idle"_s);
				m.Add("Devan"_s, "demon_turn"_s);
				m.Add("Devan"_s, "disarmed_warp_in"_s);
				m.Add("Devan"_s, "disoriented_warp_out"_s);
				m.Add("Devan"_s, "disarmed"_s);
				m.Add("Devan"_s, "crouch"_s);
				m.Add("Devan"_s, "shoot"_s);
				m.Add("Devan"_s, "disarmed_gun"_s);
				m.Add("Devan"_s, "jump"_s);
				m.Add("Devan"_s, "bullet"_s);
				m.Add("Devan"_s, "run"_s);
				m.Add("Devan"_s, "run_end"_s);
				m.Add("Devan"_s, "jump_end"_s);
				m.Add("Devan"_s, "idle"_s);
				m.Add("Devan"_s, "warp_in"_s);
				m.Add("Devan"_s, "warp_out"_s);
			}

			m.NextSet();
			m.Add("Pole"_s, "diamondus"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Doggy"_s, "attack"_s);
				m.Add("Doggy"_s, "walk"_s);

				m.NextSet(); // set 30 (all)
				//m.Add("Unimplemented"_s, "door"_s);	// Unused
				//m.Add("Unimplemented"_s, "door_enter_jazz_spaz"_s);	// Unused
				m.DiscardItems(2);
			}

			m.NextSet();
			m.Add("Dragonfly"_s, "idle"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Dragon"_s, "attack"_s);
				m.Add("Dragon"_s, "idle"_s);
				m.Add("Dragon"_s, "turn"_s);

				m.NextSet(1, JJ2Version::BaseGame | JJ2Version::HH);
				m.NextSet(2, JJ2Version::TSF | JJ2Version::CC);
			}

			m.NextSet(4);
			m.Add("Eva"_s, "blink"_s);
			m.Add("Eva"_s, "idle"_s);
			m.Add("Eva"_s, "kiss_start"_s);
			m.Add("Eva"_s, "kiss_end"_s);

			m.NextSet();
			m.Add("UI"_s, "icon_birdy"_s);
			m.Add("UI"_s, "icon_birdy_yellow"_s);
			m.Add("UI"_s, "icon_frog"_s);
			m.Add("UI"_s, "icon_jazz"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "UI"_s, "icon_lori"_s);
			m.Add("UI"_s, "icon_spaz"_s);

			if (isFull) {
				m.NextSet(2); // set 41 (1.24) / set 40 (1.23)
				m.Add("FatChick"_s, "attack"_s);
				m.Add("FatChick"_s, "walk"_s);
				m.NextSet();
				m.Add("Fencer"_s, "attack"_s);
				m.Add("Fencer"_s, "idle"_s);
				m.NextSet();
				m.Add("Fish"_s, "attack"_s);
				m.Add("Fish"_s, "idle"_s);
			}

			m.NextSet();
			m.Add("CTF"_s, "arrow"_s);
			m.Add("CTF"_s, "base"_s);
			m.Add("CTF"_s, "lights"_s);
			m.Add("CTF"_s, "flag_blue"_s);
			m.Add("UI"_s, "ctf_flag_blue"_s);
			m.Add("CTF"_s, "base_eva"_s);
			m.Add("CTF"_s, "base_eva_cheer"_s);
			m.Add("CTF"_s, "flag_red"_s);
			m.Add("UI"_s, "ctf_flag_red"_s);

			if (isFull) {
				m.NextSet();
				m.DiscardItems(1); // Strange green circles
			}

			m.NextSet();
			//m.Add("UI"_s, "font_medium"_s);
			//m.Add("UI"_s, "font_small"_s);
			//m.Add("UI"_s, "font_large"_s);
			m.DiscardItems(3);

			//m.Add("UI"_s, "logo"_s, skipNormalMap: true);
			m.DiscardItems(1);
			//m.Add(JJ2Version::CC, "UI"_s, "cc_logo"_s);
			m.DiscardItems(1, JJ2Version::CC);

			m.NextSet();
			m.Add("Frog"_s, "fall_land"_s);
			m.Add("Frog"_s, "hurt"_s);
			m.Add("Frog"_s, "idle"_s);
			m.Add("Jazz"_s, "transform_frog"_s);
			m.Add("Frog"_s, "fall"_s);
			m.Add("Frog"_s, "jump_start"_s);
			m.Add("Frog"_s, "crouch"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "transform_frog"_s);
			m.Add("Frog"_s, "tongue_diag_upright"_s);
			m.Add("Frog"_s, "tongue_hor"_s);
			m.Add("Frog"_s, "tongue_ver"_s);
			m.Add("Spaz"_s, "transform_frog"_s);
			m.Add("Frog"_s, "run"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Platform"_s, "carrotus_fruit"_s);
				m.Add("Platform"_s, "carrotus_fruit_chain"_s);
				m.NextSet();
				//m.Add("Pickup"_s, "gem_gemring"_s, keepIndexed: true);
				m.DiscardItems(1);
				m.NextSet(); // set 50 (1.24) / set 49 (1.23)
				m.Add("Unimplemented"_s, "boxing_glove_stiff"_s);
				m.Add("Unimplemented"_s, "boxing_glove_stiff_idle"_s);
				m.Add("Unimplemented"_s, "boxing_glove_normal"_s);
				m.Add("Unimplemented"_s, "boxing_glove_normal_idle"_s);
				m.Add("Unimplemented"_s, "boxing_glove_relaxed"_s);
				m.Add("Unimplemented"_s, "boxing_glove_relaxed_idle"_s);

				m.NextSet();
				m.Add("Platform"_s, "carrotus_grass"_s);
				m.Add("Platform"_s, "carrotus_grass_chain"_s);
			}

			m.NextSet();
			m.Add("MadderHatter"_s, "cup"_s);
			m.Add("MadderHatter"_s, "hat"_s);
			m.Add("MadderHatter"_s, "attack"_s);
			m.Add("MadderHatter"_s, "bullet_spit"_s);
			m.Add("MadderHatter"_s, "walk"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Helmut"_s, "idle"_s);
				m.Add("Helmut"_s, "walk"_s);
			}

			m.NextSet(2);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unknown_disoriented"_s);
			m.Add("Jazz"_s, "airboard"_s);
			m.Add("Jazz"_s, "airboard_turn"_s);
			m.Add("Jazz"_s, "buttstomp_end"_s);
			m.Add("Jazz"_s, "corpse"_s);
			m.Add("Jazz"_s, "die"_s);
			m.Add("Jazz"_s, "crouch_start"_s);
			m.Add("Jazz"_s, "crouch"_s);
			m.Add("Jazz"_s, "crouch_shoot"_s);
			m.Add("Jazz"_s, "crouch_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_door_enter"_s);
			m.Add("Jazz"_s, "vine_walk"_s);
			m.Add("Jazz"_s, "eol"_s);
			m.Add("Jazz"_s, "fall"_s);
			m.Add("Jazz"_s, "buttstomp"_s);
			m.Add("Jazz"_s, "fall_end"_s);
			m.Add("Jazz"_s, "shoot"_s);
			m.Add("Jazz"_s, "shoot_ver"_s);
			m.Add("Jazz"_s, "shoot_end"_s);
			m.Add("Jazz"_s, "transform_frog_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "ledge_climb"_s);
			m.Add("Jazz"_s, "vine_shoot_start"_s);
			m.Add("Jazz"_s, "vine_shoot_up_end"_s);
			m.Add("Jazz"_s, "vine_shoot_up"_s);
			m.Add("Jazz"_s, "vine_idle"_s);
			m.Add("Jazz"_s, "vine_idle_flavor"_s);
			m.Add("Jazz"_s, "vine_shoot_end"_s);
			m.Add("Jazz"_s, "vine_shoot"_s);
			m.Add("Jazz"_s, "copter"_s);
			m.Add("Jazz"_s, "copter_shoot_start"_s);
			m.Add("Jazz"_s, "copter_shoot"_s);
			m.Add("Jazz"_s, "pole_h"_s);
			m.Add("Jazz"_s, "hurt"_s);
			m.Add("Jazz"_s, "idle_flavor_1"_s);
			m.Add("Jazz"_s, "idle_flavor_2"_s);
			m.Add("Jazz"_s, "idle_flavor_3"_s);
			m.Add("Jazz"_s, "idle_flavor_4"_s);
			m.Add("Jazz"_s, "idle_flavor_5"_s);
			m.Add("Jazz"_s, "vine_shoot_up_start"_s);
			m.Add("Jazz"_s, "fall_shoot"_s);
			m.Add("Jazz"_s, "jump_unknown_1"_s);
			m.Add("Jazz"_s, "jump_unknown_2"_s);
			m.Add("Jazz"_s, "jump"_s);
			m.Add("Jazz"_s, "ledge"_s);
			m.Add("Jazz"_s, "lift"_s);
			m.Add("Jazz"_s, "lift_end"_s);
			m.Add("Jazz"_s, "lift_start"_s);
			m.Add("Jazz"_s, "lookup_start"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_diag_upright"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_ver_up"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_diag_upleft_reverse"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_reverse"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_diag_downleft_reverse"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_ver_down"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_diag_downright"_s);
			m.DiscardItems(7, JJ2Version::BaseGame | JJ2Version::HH);
			m.Add("Jazz"_s, "dizzy_walk"_s);
			m.Add("Jazz"_s, "push"_s);
			m.Add("Jazz"_s, "shoot_start"_s);
			m.Add("Jazz"_s, "revup_start"_s);
			m.Add("Jazz"_s, "revup"_s);
			m.Add("Jazz"_s, "revup_end"_s);
			m.Add("Jazz"_s, "fall_diag"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unknown_mid_frame"_s);
			m.Add("Jazz"_s, "jump_diag"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_jump_shoot_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_jump_shoot_ver_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_jump_shoot_ver"_s);
			m.Add("Jazz"_s, "ball"_s);
			m.Add("Jazz"_s, "run"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_run_aim_diag"_s);
			m.Add("Jazz"_s, "dash_start"_s);
			m.Add("Jazz"_s, "dash"_s);
			m.Add("Jazz"_s, "dash_stop"_s);
			m.Add("Jazz"_s, "walk_stop"_s);
			m.Add("Jazz"_s, "run_stop"_s);
			m.Add("Jazz"_s, "spring"_s);
			m.Add("Jazz"_s, "idle"_s);
			m.Add("Jazz"_s, "uppercut"_s);
			m.Add("Jazz"_s, "uppercut_end"_s);
			m.Add("Jazz"_s, "uppercut_start"_s);
			m.Add("Jazz"_s, "dizzy"_s);
			m.Add("Jazz"_s, "swim_diag_downright"_s);
			m.Add("Jazz"_s, "swim_right"_s);
			m.Add("Jazz"_s, "swim_diag_right_to_downright"_s);
			m.Add("Jazz"_s, "swim_diag_right_to_upright"_s);
			m.Add("Jazz"_s, "swim_diag_upright"_s);
			m.Add("Jazz"_s, "swing"_s);
			m.Add("Jazz"_s, "warp_in"_s);
			m.Add("Jazz"_s, "warp_out_freefall"_s);
			m.Add("Jazz"_s, "freefall"_s);
			m.Add("Jazz"_s, "warp_in_freefall"_s);
			m.Add("Jazz"_s, "warp_out"_s);
			m.Add("Jazz"_s, "pole_v"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_crouch_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_crouch_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_fall"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_hurt"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_idle"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_jump"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_crouch_end_2"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_lookup_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_run"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_unarmed_stare"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Jazz"_s, "unused_lookup_start_2"_s);

			m.NextSet();
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_idle_flavor_2"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_jump_2"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_dash_2"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_rotate_2"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_ball_2"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_run_2"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unimplemented"_s, "bonus_jazz_idle_2"_s);
			m.DiscardItems(7, JJ2Version::BaseGame | JJ2Version::HH);
			//m.Add("Unimplemented"_s, "bonus_jazz_idle_flavor"_s);
			//m.Add("Unimplemented"_s, "bonus_jazz_jump"_s);
			//m.Add("Unimplemented"_s, "bonus_jazz_ball"_s);
			//m.Add("Unimplemented"_s, "bonus_jazz_run"_s);
			//m.Add("Unimplemented"_s, "bonus_jazz_dash"_s);
			//m.Add("Unimplemented"_s, "bonus_jazz_rotate"_s);
			//m.Add("Unimplemented"_s, "bonus_jazz_idle"_s);
			m.DiscardItems(7);

			if (isFull) {
				m.NextSet(2);
				m.Add("Pole"_s, "jungle"_s);
			}

			m.NextSet();
			m.Add("LabRat"_s, "attack"_s);
			m.Add("LabRat"_s, "idle"_s);
			m.Add("LabRat"_s, "walk"_s);

			m.NextSet(); // set 60 (1.24) / set 59 (1.23)
			m.Add("Lizard"_s, "copter_attack"_s);
			m.Add("Lizard"_s, "bomb"_s);
			m.Add("Lizard"_s, "copter_idle"_s);
			m.Add("Lizard"_s, "copter"_s);
			m.Add("Lizard"_s, "walk"_s);

			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "airboard"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "airboard_turn"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "buttstomp_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "corpse"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "die"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "crouch_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "crouch"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "crouch_shoot"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "crouch_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_walk"_s);
			//m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "eol"_s);
			m.DiscardItems(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "fall"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "buttstomp"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "fall_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "shoot"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "shoot_ver"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "shoot_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "transform_frog_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_shoot_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_shoot_up_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_shoot_up"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_idle"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_idle_flavor"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_shoot_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_shoot"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "copter"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "copter_shoot_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "copter_shoot"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "pole_h"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle_flavor_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle_flavor_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle_flavor_3"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle_flavor_4"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle_flavor_5"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "vine_shoot_up_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "fall_shoot"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_unknown_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_unknown_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "ledge"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "lift"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "lift_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "lift_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "lookup_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "dizzy_walk"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "push"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "shoot_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "revup_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "revup"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "revup_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "fall_diag"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_diag"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "ball"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "run"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "dash_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "dash"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "dash_stop"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "walk_stop"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "run_stop"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "spring"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "uppercut_placeholder_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "uppercut_placeholder_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "sidekick"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "dizzy"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "swim_diag_downright"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "swim_right"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "swim_diag_right_to_downright"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "swim_diag_right_to_upright"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "swim_diag_upright"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "swing"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "warp_in"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "warp_out_freefall"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "freefall"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "warp_in_freefall"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "warp_out"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "pole_v"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "idle_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "gun"_s);

			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC);
			m.NextSet();
			//m.Add("UI"_s, "multiplayer_char"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(1);

			//m.Add("UI"_s, "multiplayer_color"_s, JJ2DefaultPalette.Menu);
			m.DiscardItems(1);

			m.Add("UI"_s, "character_art_difficulty_jazz"_s, JJ2DefaultPalette::Menu, true);
			//m.Add(JJ2Version::TSF | JJ2Version::CC, "UI"_s, "character_art_difficulty_lori"_s, JJ2DefaultPalette::Menu, true);
			m.DiscardItems(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add("UI"_s, "character_art_difficulty_spaz"_s, JJ2DefaultPalette::Menu, true);
			//m.Add("Unimplemented"_s, "key"_s, JJ2DefaultPalette::Menu, true);
			m.DiscardItems(1);

			//m.Add("UI"_s, "loading_bar"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(1);
			//m.Add("UI"_s, "multiplayer_mode"_s, JJ2DefaultPalette::Menu, true);
			m.DiscardItems(1);
			//m.Add("UI"_s, "character_name_jazz"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(1);
			//m.Add(JJ2Version::TSF | JJ2Version::CC, "UI"_s, "character_name_lori"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(1, JJ2Version::TSF | JJ2Version::CC);
			//m.Add("UI"_s, "character_name_spaz"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(1);

			//m.Add("UI"_s, "character_art_jazz"_s, JJ2DefaultPalette::Menu, true);
			m.DiscardItems(1);
			//m.Add(JJ2Version::TSF | JJ2Version::CC, "UI"_s, "character_art_lori"_s, JJ2DefaultPalette::Menu, true);
			m.DiscardItems(1, JJ2Version::TSF | JJ2Version::CC);
			//m.Add("UI"_s, "character_art_spaz"_s, JJ2DefaultPalette::Menu, true);
			m.DiscardItems(1);
			m.NextSet();

			//m.Add("UI"_s, "font_medium_2"_s, JJ2DefaultPalette.Menu);
			//m.Add("UI"_s, "font_small_2"_s, JJ2DefaultPalette.Menu);
			//m.Add("UI"_s, "logo_large"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			//m.Add(JJ2Version::TSF | JJ2Version::CC, "UI"_s, "tsf_title"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			//m.Add(JJ2Version::CC, "UI"_s, "menu_snow"_s, JJ2DefaultPalette.Menu);
			//m.Add("UI"_s, "logo_small"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			//m.Add(JJ2Version::CC, "UI"_s, "cc_title"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			//m.Add(JJ2Version::CC, "UI"_s, "cc_title_small"_s, JJ2DefaultPalette.Menu, skipNormalMap: true);
			m.DiscardItems(8);

			if (isFull) {
				m.NextSet(2);
				m.Add("Monkey"_s, "banana"_s);
				m.Add("Monkey"_s, "banana_splat"_s);
				m.Add("Monkey"_s, "jump"_s);
				m.Add("Monkey"_s, "walk_start"_s);
				m.Add("Monkey"_s, "walk_end"_s);
				m.Add("Monkey"_s, "attack"_s);
				m.Add("Monkey"_s, "walk"_s);

				m.NextSet();
				m.Add("Moth"_s, "green"_s);
				m.Add("Moth"_s, "gray"_s);
				m.Add("Moth"_s, "purple"_s);
				m.Add("Moth"_s, "pink"_s);
			} else {
				m.NextSet();
			}

			m.NextSet(3); // set 71 (1.24) / set 67 (1.23)
			m.Add("Pickup"_s, "1up"_s);
			m.Add("Pickup"_s, "food_apple"_s);
			m.Add("Pickup"_s, "food_banana"_s);
			m.Add("Object"_s, "container_barrel"_s);
			m.Add("Common"_s, "poof_brown"_s);
			m.Add("Object"_s, "container_box_crush"_s);
			m.Add("Object"_s, "container_barrel_shrapnel_1"_s);
			m.Add("Object"_s, "container_barrel_shrapnel_2"_s);
			m.Add("Object"_s, "container_barrel_shrapnel_3"_s);
			m.Add("Object"_s, "container_barrel_shrapnel_4"_s);
			m.Add("Object"_s, "powerup_shield_water"_s);
			m.Add("Pickup"_s, "food_burger"_s);
			m.Add("Pickup"_s, "food_cake"_s);
			m.Add("Pickup"_s, "food_candy"_s);
			m.Add("Object"_s, "checkpoint"_s);
			m.Add("Pickup"_s, "food_cheese"_s);
			m.Add("Pickup"_s, "food_cherry"_s);
			m.Add("Pickup"_s, "food_chicken"_s);
			m.Add("Pickup"_s, "food_chips"_s);
			m.Add("Pickup"_s, "food_chocolate"_s);
			m.Add("Pickup"_s, "food_cola"_s);
			m.Add("Pickup"_s, "carrot"_s);
			m.Add("Pickup"_s, "gem"_s, JJ2DefaultPalette::Sprite, false, true);
			m.Add("Pickup"_s, "food_cucumber"_s);
			m.Add("Pickup"_s, "food_cupcake"_s);
			m.Add("Pickup"_s, "food_donut"_s);
			m.Add("Pickup"_s, "food_eggplant"_s);
			m.Add("Unknown"_s, "green_blast_thing"_s);
			m.Add("Object"_s, "exit_sign"_s);
			m.Add("Pickup"_s, "fast_fire_jazz"_s);
			m.Add("Pickup"_s, "fast_fire_spaz"_s);
			m.Add("Object"_s, "powerup_shield_fire"_s);
			m.Add("Pickup"_s, "food_fries"_s);
			m.Add("Pickup"_s, "fast_feet"_s);
			m.Add("Object"_s, "gem_giant"_s, JJ2DefaultPalette::Sprite, false, true);

			//m.Add("Pickup"_s, "gem2"_s, keepIndexed: true);
			m.DiscardItems(1);

			m.Add("Pickup"_s, "airboard"_s);
			m.Add("Pickup"_s, "coin_gold"_s);
			m.Add("Pickup"_s, "food_grapes"_s);
			m.Add("Pickup"_s, "food_ham"_s);
			m.Add("Pickup"_s, "carrot_fly"_s);
			m.Add("UI"_s, "heart"_s, JJ2DefaultPalette::Sprite, true);
			m.Add("Pickup"_s, "freeze_enemies"_s);
			m.Add("Pickup"_s, "food_ice_cream"_s);
			m.Add("Common"_s, "ice_break_shrapnel_1"_s);
			m.Add("Common"_s, "ice_break_shrapnel_2"_s);
			m.Add("Common"_s, "ice_break_shrapnel_3"_s);
			m.Add("Common"_s, "ice_break_shrapnel_4"_s);
			m.Add("Pickup"_s, "food_lemon"_s);
			m.Add("Pickup"_s, "food_lettuce"_s);
			m.Add("Pickup"_s, "food_lime"_s);
			m.Add("Object"_s, "powerup_shield_lightning"_s);
			m.Add("Object"_s, "trigger_crate"_s);
			m.Add("Pickup"_s, "food_milk"_s);
			m.Add("Object"_s, "crate_ammo_bouncer"_s);
			m.Add("Object"_s, "crate_ammo_freezer"_s);
			m.Add("Object"_s, "crate_ammo_seeker"_s);
			m.Add("Object"_s, "crate_ammo_rf"_s);
			m.Add("Object"_s, "crate_ammo_toaster"_s);
			m.Add("Object"_s, "crate_ammo_tnt"_s);
			m.Add("Object"_s, "powerup_upgrade_blaster_jazz"_s);
			m.Add("Object"_s, "powerup_upgrade_bouncer"_s);
			m.Add("Object"_s, "powerup_upgrade_freezer"_s);
			m.Add("Object"_s, "powerup_upgrade_seeker"_s);
			m.Add("Object"_s, "powerup_upgrade_rf"_s);
			m.Add("Object"_s, "powerup_upgrade_toaster"_s);
			m.Add("Object"_s, "powerup_upgrade_pepper"_s);
			m.Add("Object"_s, "powerup_upgrade_electro"_s);
			m.Add("Object"_s, "powerup_transform_birdy"_s);
			m.Add("Object"_s, "powerup_transform_birdy_yellow"_s);
			m.Add("Object"_s, "powerup_swap_characters"_s);
			m.Add("Pickup"_s, "food_orange"_s);
			m.Add("Pickup"_s, "carrot_invincibility"_s);
			m.Add("Pickup"_s, "food_peach"_s);
			m.Add("Pickup"_s, "food_pear"_s);
			m.Add("Pickup"_s, "food_soda"_s);
			m.Add("Pickup"_s, "food_pie"_s);
			m.Add("Pickup"_s, "food_pizza"_s);
			m.Add("Pickup"_s, "potion"_s);
			m.Add("Pickup"_s, "food_pretzel"_s);
			m.Add("Pickup"_s, "food_sandwich"_s);
			m.Add("Pickup"_s, "food_strawberry"_s);
			m.Add("Pickup"_s, "carrot_full"_s);
			m.Add("Object"_s, "powerup_upgrade_blaster_spaz"_s);
			m.Add("Pickup"_s, "coin_silver"_s);
			m.Add("Unknown"_s, "green_blast_thing_2"_s);
			m.Add("Common"_s, "generator"_s);
			m.Add("Pickup"_s, "stopwatch"_s);
			m.Add("Pickup"_s, "food_taco"_s);
			m.Add("Pickup"_s, "food_thing"_s);
			m.Add("Object"_s, "tnt"_s);
			m.Add("Pickup"_s, "food_hotdog"_s);
			m.Add("Pickup"_s, "food_watermelon"_s);
			m.Add("Object"_s, "container_crate_shrapnel_1"_s);
			m.Add("Object"_s, "container_crate_shrapnel_2"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Pinball"_s, "bumper_500"_s);
				m.Add("Pinball"_s, "bumper_500_hit"_s);
				m.Add("Pinball"_s, "bumper_carrot"_s);
				m.Add("Pinball"_s, "bumper_carrot_hit"_s);

				m.Add("Pinball"_s, "paddle_left"_s, JJ2DefaultPalette::Sprite, false, 1);
				//m.Add("Pinball"_s, "paddle_right"_s, JJ2DefaultPalette.ByIndex);
				m.DiscardItems(1);

				m.NextSet();
				m.Add("Platform"_s, "lab"_s);
				m.Add("Platform"_s, "lab_chain"_s);

				m.NextSet();
				m.Add("Pole"_s, "psych"_s);

				m.NextSet();
				m.Add("Queen"_s, "scream"_s);
				m.Add("Queen"_s, "ledge"_s);
				m.Add("Queen"_s, "ledge_recover"_s);
				m.Add("Queen"_s, "idle"_s);
				m.Add("Queen"_s, "brick"_s);
				m.Add("Queen"_s, "fall"_s);
				m.Add("Queen"_s, "stomp"_s);
				m.Add("Queen"_s, "backstep"_s);

				m.NextSet();
				m.Add("Rapier"_s, "attack"_s);
				m.Add("Rapier"_s, "attack_swing"_s);
				m.Add("Rapier"_s, "idle"_s);
				m.Add("Rapier"_s, "attack_start"_s);
				m.Add("Rapier"_s, "attack_end"_s);

				m.NextSet();
				m.Add("Raven"_s, "attack"_s);
				m.Add("Raven"_s, "idle"_s);
				m.Add("Raven"_s, "turn"_s);

				m.NextSet();
				m.Add("Robot"_s, "spike_ball"_s);
				m.Add("Robot"_s, "attack_start"_s);
				m.Add("Robot"_s, "attack"_s);
				m.Add("Robot"_s, "copter"_s);
				m.Add("Robot"_s, "idle"_s);
				m.Add("Robot"_s, "attack_end"_s);
				m.Add("Robot"_s, "shrapnel_1"_s);
				m.Add("Robot"_s, "shrapnel_2"_s);
				m.Add("Robot"_s, "shrapnel_3"_s);
				m.Add("Robot"_s, "shrapnel_4"_s);
				m.Add("Robot"_s, "shrapnel_5"_s);
				m.Add("Robot"_s, "shrapnel_6"_s);
				m.Add("Robot"_s, "shrapnel_7"_s);
				m.Add("Robot"_s, "shrapnel_8"_s);
				m.Add("Robot"_s, "shrapnel_9"_s);
				m.Add("Robot"_s, "run"_s);
				m.Add("Robot"_s, "copter_start"_s);
				m.Add("Robot"_s, "copter_end"_s);

				m.NextSet();
				m.Add("Object"_s, "rolling_rock"_s);

				m.NextSet(); // set 80 (1.24) / set 76 (1.23)
				m.Add("TurtleRocket"_s, "downright"_s);
				m.Add("TurtleRocket"_s, "upright"_s);
				m.Add("TurtleRocket"_s, "smoke"_s);
				m.Add("TurtleRocket"_s, "upright_to_downright"_s);

				m.NextSet(3);
				m.Add("Skeleton"_s, "bone"_s);
				m.Add("Skeleton"_s, "skull"_s);
				m.Add("Skeleton"_s, "walk"_s);
			} else {
				m.NextSet();
			}

			m.NextSet();
			m.Add("Pole"_s, "diamondus_tree"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Common"_s, "snow"_s);

				m.NextSet();
				m.Add("Bolly"_s, "rocket"_s);
				m.Add("Bolly"_s, "mace_chain"_s);
				m.Add("Bolly"_s, "bottom"_s);
				m.Add("Bolly"_s, "top"_s);
				m.Add("Bolly"_s, "puff"_s);
				m.Add("Bolly"_s, "mace"_s);
				m.Add("Bolly"_s, "turret"_s);
				m.Add("Bolly"_s, "crosshairs"_s);
				m.NextSet();
				m.Add("Platform"_s, "sonic"_s);
				m.Add("Platform"_s, "sonic_chain"_s);
				m.NextSet();
				m.Add("Sparks"_s, "idle"_s);
			}

			m.NextSet();
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unknown_disoriented"_s);
			m.Add("Spaz"_s, "airboard"_s);
			m.Add("Spaz"_s, "airboard_turn"_s);
			m.Add("Spaz"_s, "buttstomp_end"_s);
			m.Add("Spaz"_s, "corpse"_s);
			m.Add("Spaz"_s, "die"_s);
			m.Add("Spaz"_s, "crouch_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "crouch_shoot_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Spaz"_s, "crouch"_s);
			m.Add("Spaz"_s, "crouch_shoot"_s);
			m.Add("Spaz"_s, "crouch_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_door_enter"_s);
			m.Add("Spaz"_s, "vine_walk"_s);
			m.Add("Spaz"_s, "eol"_s);
			m.Add("Spaz"_s, "fall"_s);
			m.Add("Spaz"_s, "buttstomp"_s);
			m.Add("Spaz"_s, "fall_end"_s);
			m.Add("Spaz"_s, "shoot"_s);
			m.Add("Spaz"_s, "shoot_ver"_s);
			m.Add("Spaz"_s, "shoot_end"_s);
			m.Add("Spaz"_s, "transform_frog_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "ledge_climb"_s);
			m.Add("Spaz"_s, "vine_shoot_start"_s);
			m.Add("Spaz"_s, "vine_shoot_up_end"_s);
			m.Add("Spaz"_s, "vine_shoot_up"_s);
			m.Add("Spaz"_s, "vine_idle"_s);
			m.Add("Spaz"_s, "vine_idle_flavor"_s);
			m.Add("Spaz"_s, "vine_shoot_end"_s);
			m.Add("Spaz"_s, "vine_shoot"_s);
			m.Add("Spaz"_s, "copter"_s);
			m.Add("Spaz"_s, "copter_shoot_start"_s);
			m.Add("Spaz"_s, "copter_shoot"_s);
			m.Add("Spaz"_s, "pole_h"_s);
			m.Add("Spaz"_s, "hurt"_s);
			m.Add("Spaz"_s, "idle_flavor_1"_s);
			m.Add("Spaz"_s, "idle_flavor_2"_s);
			m.Add("Spaz"_s, "idle_flavor_3_placeholder"_s);
			m.Add("Spaz"_s, "idle_flavor_4"_s);
			m.Add("Spaz"_s, "idle_flavor_5"_s);
			m.Add("Spaz"_s, "vine_shoot_up_start"_s);
			m.Add("Spaz"_s, "fall_shoot"_s);
			m.Add("Spaz"_s, "jump_unknown_1"_s);
			m.Add("Spaz"_s, "jump_unknown_2"_s);
			m.Add("Spaz"_s, "jump"_s);
			m.Add("Spaz"_s, "ledge"_s);
			m.Add("Spaz"_s, "lift"_s);
			m.Add("Spaz"_s, "lift_end"_s);
			m.Add("Spaz"_s, "lift_start"_s);
			m.Add("Spaz"_s, "lookup_start"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_diag_upright"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_ver_up"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_diag_upleft_reverse"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_reverse"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_diag_downleft_reverse"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_ver_down"_s);
			//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_diag_downright"_s);
			m.DiscardItems(7, JJ2Version::BaseGame | JJ2Version::HH);
			m.Add("Spaz"_s, "dizzy_walk"_s);
			m.Add("Spaz"_s, "push"_s);
			m.Add("Spaz"_s, "shoot_start"_s);
			m.Add("Spaz"_s, "revup_start"_s);
			m.Add("Spaz"_s, "revup"_s);
			m.Add("Spaz"_s, "revup_end"_s);
			m.Add("Spaz"_s, "fall_diag"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unknown_mid_frame"_s);
			m.Add("Spaz"_s, "jump_diag"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_jump_shoot_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_jump_shoot_ver_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_jump_shoot_ver"_s);
			m.Add("Spaz"_s, "ball"_s);
			m.Add("Spaz"_s, "run"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_run_aim_diag"_s);
			m.Add("Spaz"_s, "dash_start"_s);
			m.Add("Spaz"_s, "dash"_s);
			m.Add("Spaz"_s, "dash_stop"_s);
			m.Add("Spaz"_s, "walk_stop"_s);
			m.Add("Spaz"_s, "run_stop"_s);
			m.Add("Spaz"_s, "spring"_s);
			m.Add("Spaz"_s, "idle"_s);
			m.Add("Spaz"_s, "sidekick"_s);
			m.Add("Spaz"_s, "sidekick_end"_s);
			m.Add("Spaz"_s, "sidekick_start"_s);
			m.Add("Spaz"_s, "dizzy"_s);
			m.Add("Spaz"_s, "swim_diag_downright"_s);
			m.Add("Spaz"_s, "swim_right"_s);
			m.Add("Spaz"_s, "swim_diag_right_to_downright"_s);
			m.Add("Spaz"_s, "swim_diag_right_to_upright"_s);
			m.Add("Spaz"_s, "swim_diag_upright"_s);
			m.Add("Spaz"_s, "swing"_s);
			m.Add("Spaz"_s, "warp_in"_s);
			m.Add("Spaz"_s, "warp_out_freefall"_s);
			m.Add("Spaz"_s, "freefall"_s);
			m.Add("Spaz"_s, "warp_in_freefall"_s);
			m.Add("Spaz"_s, "warp_out"_s);
			m.Add("Spaz"_s, "pole_v"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_crouch_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_crouch_end"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_fall"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_hurt"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_idle"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_jump"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_crouch_end_2"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_lookup_start"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_run"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_unarmed_stare"_s);
			m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Spaz"_s, "unused_lookup_start_2"_s);
			m.NextSet(); // set 90 (1.24) / set 86 (1.23)
			m.Add("Spaz"_s, "idle_flavor_3_start"_s);
			m.Add("Spaz"_s, "idle_flavor_3"_s);
			m.Add("Spaz"_s, "idle_flavor_3_bird"_s);
			m.Add("Spaz"_s, "idle_flavor_5_spaceship"_s);

			if (isFull) {
				m.NextSet();
				//m.Add("Unimplemented"_s, "bonus_spaz_idle_flavor"_s);
				//m.Add("Unimplemented"_s, "bonus_spaz_jump"_s);
				//m.Add("Unimplemented"_s, "bonus_spaz_ball"_s);
				//m.Add("Unimplemented"_s, "bonus_spaz_run"_s);
				//m.Add("Unimplemented"_s, "bonus_spaz_dash"_s);
				//m.Add("Unimplemented"_s, "bonus_spaz_rotate"_s);
				//m.Add("Unimplemented"_s, "bonus_spaz_idle"_s);
				m.DiscardItems(7);

				m.NextSet(2);
				m.Add("Object"_s, "3d_spike"_s);
				m.Add("Object"_s, "3d_spike_chain"_s);

				m.NextSet();
				//m.Add("Object"_s, "3d_spike_2"_s);
				//m.Add("Object"_s, "3d_spike_2_chain"_s);
				m.DiscardItems(2);

				m.NextSet();
				m.Add("Platform"_s, "spike"_s);
				m.Add("Platform"_s, "spike_chain"_s);
			} else {
				m.NextSet();
			}

			m.NextSet();
			m.Add("Spring"_s, "spring_blue_ver"_s);
			m.Add("Spring"_s, "spring_blue_hor"_s);
			m.Add("Spring"_s, "spring_blue_ver_reverse"_s);
			m.Add("Spring"_s, "spring_green_ver_reverse"_s);
			m.Add("Spring"_s, "spring_red_ver_reverse"_s);
			m.Add("Spring"_s, "spring_green_ver"_s);
			m.Add("Spring"_s, "spring_green_hor"_s);
			m.Add("Spring"_s, "spring_red_ver"_s);
			m.Add("Spring"_s, "spring_red_hor"_s);

			m.NextSet();
			m.Add("Common"_s, "steam_note"_s);

			if (isFull) {
				m.NextSet();
			}

			m.NextSet();
			m.Add("Sucker"_s, "walk_top"_s);
			m.Add("Sucker"_s, "inflated_deflate"_s);
			m.Add("Sucker"_s, "walk_ver_down"_s);
			m.Add("Sucker"_s, "fall"_s);
			m.Add("Sucker"_s, "inflated"_s);
			m.Add("Sucker"_s, "poof"_s);
			m.Add("Sucker"_s, "walk"_s);
			m.Add("Sucker"_s, "walk_ver_up"_s);

			if (isFull) {
				m.NextSet(); // set 100 (1.24) / set 96 (1.23)
				m.Add("TurtleTube"_s, "idle"_s);

				m.NextSet();
				m.Add("TurtleBoss"_s, "attack_start"_s);
				m.Add("TurtleBoss"_s, "attack_end"_s);
				m.Add("TurtleBoss"_s, "shell"_s);
				m.Add("TurtleBoss"_s, "mace"_s);
				m.Add("TurtleBoss"_s, "idle"_s);
				m.Add("TurtleBoss"_s, "walk"_s);

				m.NextSet();
				m.Add("TurtleTough"_s, "walk"_s);
			}

			m.NextSet();
			m.Add("Turtle"_s, "attack"_s);
			m.Add("Turtle"_s, "idle_flavor"_s);
			m.Add("Turtle"_s, "turn_start"_s);
			m.Add("Turtle"_s, "turn_end"_s);
			m.Add("Turtle"_s, "shell_reverse"_s);
			m.Add("Turtle"_s, "shell"_s);
			m.Add("Turtle"_s, "idle"_s);
			m.Add("Turtle"_s, "walk"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Tweedle"_s, "magnet_start"_s);
				m.Add("Tweedle"_s, "spin"_s);
				m.Add("Tweedle"_s, "magnet_end"_s);
				m.Add("Tweedle"_s, "shoot_jazz"_s);
				m.Add("Tweedle"_s, "shoot_spaz"_s);
				m.Add("Tweedle"_s, "hurt"_s);
				m.Add("Tweedle"_s, "idle"_s);
				m.Add("Tweedle"_s, "magnet"_s);
				m.Add("Tweedle"_s, "walk"_s);

				m.NextSet();
				m.Add("Uterus"_s, "closed_start"_s);
				m.Add("Crab"_s, "fall"_s);
				m.Add("Uterus"_s, "closed_idle"_s);
				m.Add("Uterus"_s, "idle"_s);
				m.Add("Crab"_s, "fall_end"_s);
				m.Add("Uterus"_s, "closed_end"_s);
				m.Add("Uterus"_s, "shield"_s);
				m.Add("Crab"_s, "walk"_s);

				m.NextSet();
				m.DiscardItems(1); // Red dot

				m.Add("Object"_s, "vine"_s);
				m.NextSet();
				m.Add("Object"_s, "bonus10"_s);
				m.NextSet();
				m.Add("Object"_s, "bonus100"_s);
			}

			m.NextSet();
			m.Add("Object"_s, "bonus20"_s);

			if (isFull) {
				m.NextSet(); // set 110 (1.24) / set 106 (1.23)
				m.Add("Object"_s, "bonus50"_s);
			}

			m.NextSet(2);
			m.Add("Witch"_s, "attack"_s);
			m.Add("Witch"_s, "die"_s);
			m.Add("Witch"_s, "idle"_s);
			m.Add("Witch"_s, "bullet_magic"_s);

			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_throw_fireball"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_appear"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_vanish"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_bullet_fireball"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_idle"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_copter_attack"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_bomb"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_copter_idle"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_copter"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_walk"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_attack"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_idle_flavor"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_turn_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_turn_end"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_shell_reverse"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_shell"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_idle"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_walk"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Doggy"_s, "xmas_attack"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Doggy"_s, "xmas_walk"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Sparks"_s, "ghost_idle"_s);
		}

		return m;
	}

	AnimSetMapping AnimSetMapping::GetSampleMapping(JJ2Version version)
	{
		AnimSetMapping m(version);

		if (version == JJ2Version::PlusExtension) {
			// Nothing is here...
		} else if (version != JJ2Version::Unknown) {
			bool isFull = ((version & JJ2Version::SharewareDemo) != JJ2Version::SharewareDemo);

			// set 0 (all)
			m.Add("Weapon"_s, "shield_water_1"_s);
			m.Add("Weapon"_s, "shield_water_2"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded_1"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded_2"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded_3"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded_4"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded_5"_s);
			m.Add("Weapon"_s, "bullet_bouncer_upgraded_6"_s);
			m.Add("Weapon"_s, "tnt_explosion"_s);
			m.Add("Weapon"_s, "ricochet_contact"_s);
			m.Add("Weapon"_s, "ricochet_bullet_1"_s);
			m.Add("Weapon"_s, "ricochet_bullet_2"_s);
			m.Add("Weapon"_s, "ricochet_bullet_3"_s);
			m.Add("Weapon"_s, "shield_fire_noise_1"_s);
			m.Add("Weapon"_s, "shield_fire_noise_2"_s);
			m.Add("Weapon"_s, "bullet_bouncer_1"_s);
			m.Add("Weapon"_s, "bullet_blaster_jazz_1"_s);
			m.Add("Weapon"_s, "bullet_blaster_jazz_2"_s);
			m.Add("Weapon"_s, "bullet_blaster_jazz_3"_s);
			m.Add("Weapon"_s, "bullet_bouncer_2"_s);
			m.Add("Weapon"_s, "bullet_bouncer_3"_s);
			m.Add("Weapon"_s, "bullet_bouncer_4"_s);
			m.Add("Weapon"_s, "bullet_bouncer_5"_s);
			m.Add("Weapon"_s, "bullet_bouncer_6"_s);
			m.Add("Weapon"_s, "bullet_bouncer_7"_s);
			m.Add("Weapon"_s, "bullet_blaster_jazz_4"_s);
			m.Add("Weapon"_s, "bullet_pepper"_s);
			m.Add("Weapon"_s, "bullet_freezer_1"_s);
			m.Add("Weapon"_s, "bullet_freezer_2"_s);
			m.Add("Weapon"_s, "bullet_freezer_upgraded_1"_s);
			m.Add("Weapon"_s, "bullet_freezer_upgraded_2"_s);
			m.Add("Weapon"_s, "bullet_freezer_upgraded_3"_s);
			m.Add("Weapon"_s, "bullet_freezer_upgraded_4"_s);
			m.Add("Weapon"_s, "bullet_freezer_upgraded_5"_s);
			m.Add("Weapon"_s, "bullet_electro_1"_s);
			m.Add("Weapon"_s, "bullet_electro_2"_s);
			m.Add("Weapon"_s, "bullet_electro_3"_s);
			m.Add("Weapon"_s, "bullet_rf"_s);
			m.Add("Weapon"_s, "bullet_seeker"_s);
			m.Add("Weapon"_s, "bullet_blaster_spaz_1"_s);
			m.Add("Weapon"_s, "bullet_blaster_spaz_2"_s);
			m.Add("Weapon"_s, "bullet_blaster_spaz_3"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Bat"_s, "noise"_s);
				m.NextSet(6);
				m.Add("Bilsy"_s, "appear_2"_s);
				m.Add("Bilsy"_s, "snap"_s);
				m.Add("Bilsy"_s, "throw_fireball"_s);
				m.Add("Bilsy"_s, "fire_start"_s);
				m.Add("Bilsy"_s, "scary"_s);
				m.Add("Bilsy"_s, "thunder"_s);
				m.Add("Bilsy"_s, "appear_1"_s);
				m.NextSet(4); // set 11 (all)
				m.Add("Unknown"_s, "unknown_bonus1"_s);
				m.Add("Unknown"_s, "unknown_bonusblub"_s);
			} else {
				m.NextSet();
			}

			m.NextSet(3); // set 14
			m.Add("Bubba"_s, "hop_1"_s);
			m.Add("Bubba"_s, "hop_2"_s);
			m.Add("Bubba"_s, "unknown_bubbaexplo"_s);
			m.Add("Bubba"_s, "unknown_frog2"_s);
			m.Add("Bubba"_s, "unknown_frog3"_s);
			m.Add("Bubba"_s, "unknown_frog4"_s);
			m.Add("Bubba"_s, "unknown_frog5"_s);
			m.Add("Bubba"_s, "sneeze"_s);
			m.Add("Bubba"_s, "tornado"_s);
			m.NextSet(); // set 15
			m.Add("Bee"_s, "noise"_s);

			if (isFull) {
				m.NextSet(3);
			}

			m.NextSet(2); // set 20 (all)
			m.Add("Caterpillar"_s, "dizzy"_s);

			if (isFull) {
				m.NextSet();
			}

			m.NextSet();
			m.Add("Common"_s, "char_airboard"_s);
			m.Add("Common"_s, "char_airboard_turn_1"_s);
			m.Add("Common"_s, "char_airboard_turn_2"_s);
			m.Add("Common"_s, "unknown_base"_s);
			m.Add("Common"_s, "powerup_shield_damage_1"_s);
			m.Add("Common"_s, "powerup_shield_damage_2"_s);
			m.Add("Common"_s, "bomb"_s);
			m.Add("Birdy"_s, "fly_1"_s);
			m.Add("Birdy"_s, "fly_2"_s);
			m.Add("Weapon"_s, "bouncer"_s);
			m.Add("Common"_s, "blub1"_s);
			m.Add("Weapon"_s, "shield_water_bullet"_s);
			m.Add("Weapon"_s, "shield_fire_bullet"_s);
			m.Add("Common"_s, "ambient_fire"_s);
			m.Add("Object"_s, "container_barrel_break"_s);
			m.Add("Common"_s, "powerup_shield_timer"_s);
			m.Add("Pickup"_s, "coin"_s);
			m.Add("Common"_s, "scenery_collapse"_s);
			m.Add("Common"_s, "cup"_s);
			m.Add("Common"_s, "scenery_destruct"_s);
			m.Add("Common"_s, "down"_s);
			m.Add("Common"_s, "downfl2"_s);
			m.Add("Pickup"_s, "food_drink_1"_s);
			m.Add("Pickup"_s, "food_drink_2"_s);
			m.Add("Pickup"_s, "food_drink_3"_s);
			m.Add("Pickup"_s, "food_drink_4"_s);
			m.Add("Pickup"_s, "food_edible_1"_s);
			m.Add("Pickup"_s, "food_edible_2"_s);
			m.Add("Pickup"_s, "food_edible_3"_s);
			m.Add("Pickup"_s, "food_edible_4"_s);
			m.Add("Pickup"_s, "shield_lightning_bullet_1"_s);
			m.Add("Pickup"_s, "shield_lightning_bullet_2"_s);
			m.Add("Pickup"_s, "shield_lightning_bullet_3"_s);
			m.Add("Weapon"_s, "tnt"_s);
			m.Add("Weapon"_s, "wall_poof"_s);
			m.Add("Weapon"_s, "toaster"_s);
			m.Add("Common"_s, "flap"_s);
			m.Add("Common"_s, "swish_9"_s);
			m.Add("Common"_s, "swish_10"_s);
			m.Add("Common"_s, "swish_11"_s);
			m.Add("Common"_s, "swish_12"_s);
			m.Add("Common"_s, "swish_13"_s);
			m.Add("Object"_s, "gem_giant_break"_s);
			m.Add("Object"_s, "powerup_break"_s);
			m.Add("Common"_s, "gunsm1"_s);
			m.Add("Pickup"_s, "1up"_s);
			m.Add("Unknown"_s, "common_head"_s);
			m.Add("Common"_s, "copter_noise"_s);
			m.Add("Common"_s, "hibell"_s);
			m.Add("Common"_s, "holyflut"_s);
			m.Add("UI"_s, "weapon_change"_s);
			m.Add("Common"_s, "ice_break"_s);
			m.Add("Object"_s, "shell_noise_1"_s);
			m.Add("Object"_s, "shell_noise_2"_s);
			m.Add("Object"_s, "shell_noise_3"_s);
			m.Add("Object"_s, "shell_noise_4"_s);
			m.Add("Object"_s, "shell_noise_5"_s);
			m.Add("Object"_s, "shell_noise_6"_s);
			m.Add("Object"_s, "shell_noise_7"_s);
			m.Add("Object"_s, "shell_noise_8"_s);
			m.Add("Object"_s, "shell_noise_9"_s);
			m.Add("Unknown"_s, "common_itemtre"_s);
			m.Add("Common"_s, "char_jump"_s);
			m.Add("Common"_s, "char_jump_alt"_s);
			m.Add("Common"_s, "land1"_s);
			m.Add("Common"_s, "land2"_s);
			m.Add("Common"_s, "land3"_s);
			m.Add("Common"_s, "land4"_s);
			m.Add("Common"_s, "land5"_s);
			m.Add("Common"_s, "char_land"_s);
			m.Add("Common"_s, "loadjazz"_s);
			m.Add("Common"_s, "loadspaz"_s);
			m.Add("Common"_s, "metalhit"_s);
			m.Add("Unimplemented"_s, "powerup_jazz1_style"_s);
			m.Add("Object"_s, "bonus_not_enough_coins"_s);
			m.Add("Pickup"_s, "gem"_s);
			m.Add("Pickup"_s, "ammo"_s);
			m.Add("Common"_s, "pistol1"_s);
			m.Add("Common"_s, "plop_5"_s);
			m.Add("Common"_s, "plop_1"_s);
			m.Add("Common"_s, "plop_2"_s);
			m.Add("Common"_s, "plop_3"_s);
			m.Add("Common"_s, "plop_4"_s);
			m.Add("Common"_s, "plop_6"_s);
			m.Add("Spaz"_s, "idle_flavor_4_spaceship"_s);
			m.Add("Common"_s, "copter_pre"_s);
			m.Add("Common"_s, "char_revup"_s);
			m.Add("Common"_s, "ringgun1"_s);
			m.Add("Common"_s, "ringgun2"_s);
			m.Add("Weapon"_s, "shield_lightning_noise_1"_s);
			m.Add("Weapon"_s, "shield_lightning_noise_2"_s);
			m.Add("Weapon"_s, "shield_lightning_noise_3"_s);
			m.Add("Common"_s, "shldof3"_s);
			m.Add("Common"_s, "slip"_s);
			m.Add("Common"_s, "splat_1"_s);
			m.Add("Common"_s, "splat_2"_s);
			m.Add("Common"_s, "splat_3"_s);
			m.Add("Common"_s, "splat_4"_s);
			m.Add("Common"_s, "splat_5"_s);
			m.Add("Common"_s, "splat_6"_s);
			m.Add("Spring"_s, "spring_2"_s);
			m.Add("Common"_s, "steam_low"_s);
			m.Add("Common"_s, "step"_s);
			m.Add("Common"_s, "stretch"_s);
			m.Add("Common"_s, "swish_1"_s);
			m.Add("Common"_s, "swish_2"_s);
			m.Add("Common"_s, "swish_3"_s);
			m.Add("Common"_s, "swish_4"_s);
			m.Add("Common"_s, "swish_5"_s);
			m.Add("Common"_s, "swish_6"_s);
			m.Add("Common"_s, "swish_7"_s);
			m.Add("Common"_s, "swish_8"_s);
			m.Add("Common"_s, "warp_in"_s);
			m.Add("Common"_s, "warp_out"_s);
			m.Add("Common"_s, "char_double_jump"_s);
			m.Add("Common"_s, "water_splash"_s);
			m.Add("Object"_s, "container_crate_break"_s);

			if (isFull) {
				m.NextSet(2);
				m.Add("Demon"_s, "attack"_s);
				m.NextSet(3);
				m.Add("Devan"_s, "spit_fireball"_s);
				m.Add("Devan"_s, "flap"_s);
				m.Add("Devan"_s, "unknown_frog4"_s);
				m.Add("Devan"_s, "jump_up"_s);
				m.Add("Devan"_s, "laugh"_s);
				m.Add("Devan"_s, "shoot"_s);
				m.Add("Devan"_s, "transform_demon_stretch_2"_s);
				m.Add("Devan"_s, "transform_demon_stretch_4"_s);
				m.Add("Devan"_s, "transform_demon_stretch_1"_s);
				m.Add("Devan"_s, "transform_demon_stretch_3"_s);
				m.Add("Devan"_s, "unknown_vanish"_s);
				m.Add("Devan"_s, "unknown_whistledescending2"_s);
				m.Add("Devan"_s, "transform_demon_wings"_s);
				m.NextSet(2);
				m.Add("Doggy"_s, "attack"_s);
				m.Add("Doggy"_s, "noise"_s);
				m.Add("Doggy"_s, "woof_1"_s);
				m.Add("Doggy"_s, "woof_2"_s);
				m.Add("Doggy"_s, "woof_3"_s);
			} else {
				m.NextSet(2);
			}

			m.NextSet(2); // set 31 (all)
			m.Add("Dragonfly"_s, "noise"_s);

			if (isFull) {
				m.NextSet(2);
				m.Add("Cinematics"_s, "ending_eva_thankyou"_s);
			}

			m.NextSet();
			m.Add("Jazz"_s, "level_complete"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "level_complete"_s);
			m.NextSet();
			m.Add("Spaz"_s, "level_complete"_s);

			m.NextSet();
			//m.Add("Cinematics"_s, "logo_epic_1"_s);
			//m.Add("Cinematics"_s, "logo_epic_2"_s);
			m.DiscardItems(2);

			m.NextSet();
			m.Add("Eva"_s, "Kiss1"_s);
			m.Add("Eva"_s, "Kiss2"_s);
			m.Add("Eva"_s, "Kiss3"_s);
			m.Add("Eva"_s, "Kiss4"_s);

			if (isFull) {
				m.NextSet(2); // set 40 (1.24) / set 39 (1.23)
				m.Add("Unknown"_s, "unknown_fan"_s);
				m.NextSet();
				m.Add("FatChick"_s, "attack_1"_s);
				m.Add("FatChick"_s, "attack_2"_s);
				m.Add("FatChick"_s, "attack_3"_s);
				m.NextSet();
				m.Add("Fencer"_s, "attack"_s);
				m.NextSet();
			}

			m.NextSet(4);
			m.Add("Frog"_s, "noise_1"_s);
			m.Add("Frog"_s, "noise_2"_s);
			m.Add("Frog"_s, "noise_3"_s);
			m.Add("Frog"_s, "noise_4"_s);
			m.Add("Frog"_s, "noise_5"_s);
			m.Add("Frog"_s, "noise_6"_s);
			m.Add("Frog"_s, "transform"_s);
			m.Add("Frog"_s, "tongue"_s);

			if (isFull) {
				m.NextSet(3); // set 50 (1.24) / set 49 (1.23)
				m.Add("Unimplemented"_s, "boxing_glove_hit"_s);
				m.NextSet();
			}

			m.NextSet();
			m.Add("MadderHatter"_s, "cup"_s);
			m.Add("MadderHatter"_s, "hat"_s);
			m.Add("MadderHatter"_s, "spit"_s);
			m.Add("MadderHatter"_s, "splash_1"_s);
			m.Add("MadderHatter"_s, "splash_2"_s);

			if (isFull) {
				m.NextSet();
			}

			m.NextSet();
			m.Add("Cinematics"_s, "opening_blow"_s);
			m.Add("Cinematics"_s, "opening_boom_1"_s);
			m.Add("Cinematics"_s, "opening_boom_2"_s);
			m.Add("Cinematics"_s, "opening_brake"_s);
			m.Add("Cinematics"_s, "opening_end_shoot"_s);
			m.Add("Cinematics"_s, "opening_rope_grab"_s);
			m.Add("Cinematics"_s, "opening_sweep_1"_s);
			m.Add("Cinematics"_s, "opening_sweep_2"_s);
			m.Add("Cinematics"_s, "opening_sweep_3"_s);
			m.Add("Cinematics"_s, "opening_gun_noise_1"_s);
			m.Add("Cinematics"_s, "opening_gun_noise_2"_s);
			m.Add("Cinematics"_s, "opening_gun_noise_3"_s);
			m.Add("Cinematics"_s, "opening_helicopter"_s);
			m.Add("Cinematics"_s, "opening_hit_spaz"_s);
			m.Add("Cinematics"_s, "opening_hit_turtle"_s);
			m.Add("Cinematics"_s, "opening_vo_1"_s);
			m.Add("Cinematics"_s, "opening_gun_blow"_s);
			m.Add("Cinematics"_s, "opening_insect"_s);
			m.Add("Cinematics"_s, "opening_trolley_push"_s);
			m.Add("Cinematics"_s, "opening_land"_s);
			m.Add("Cinematics"_s, "opening_turtle_growl"_s);
			m.Add("Cinematics"_s, "opening_turtle_grunt"_s);
			m.Add("Cinematics"_s, "opening_rock"_s);
			m.Add("Cinematics"_s, "opening_rope_1"_s);
			m.Add("Cinematics"_s, "opening_rope_2"_s);
			m.Add("Cinematics"_s, "opening_run"_s);
			m.Add("Cinematics"_s, "opening_shot"_s);
			m.Add("Cinematics"_s, "opening_shot_grn"_s);
			m.Add("Cinematics"_s, "opening_slide"_s);
			m.Add("Cinematics"_s, "opening_end_sfx"_s);
			m.Add("Cinematics"_s, "opening_swish_1"_s);
			m.Add("Cinematics"_s, "opening_swish_2"_s);
			m.Add("Cinematics"_s, "opening_swish_3"_s);
			m.Add("Cinematics"_s, "opening_swish_4"_s);
			m.Add("Cinematics"_s, "opening_turtle_ugh"_s);
			m.Add("Cinematics"_s, "opening_up_1"_s);
			m.Add("Cinematics"_s, "opening_up_2"_s);
			m.Add("Cinematics"_s, "opening_wind"_s);

			if (isFull) {
				m.NextSet();
			}

			m.NextSet(2);
			m.Add("Jazz"_s, "ledge"_s);
			m.Add("Jazz"_s, "hurt_1"_s);
			m.Add("Jazz"_s, "hurt_2"_s);
			m.Add("Jazz"_s, "hurt_3"_s);
			m.Add("Jazz"_s, "hurt_4"_s);
			m.Add("Jazz"_s, "idle_flavor_3"_s);
			m.Add("Jazz"_s, "hurt_5"_s);
			m.Add("Jazz"_s, "hurt_6"_s);
			m.Add("Jazz"_s, "hurt_7"_s);
			m.Add("Jazz"_s, "hurt_8"_s);
			m.Add("Jazz"_s, "carrot"_s);
			m.Add("Jazz"_s, "idle_flavor_4"_s);

			if (isFull) {
				m.NextSet();
			}

			m.NextSet();
			m.Add("LabRat"_s, "attack"_s);
			m.Add("LabRat"_s, "noise_1"_s);
			m.Add("LabRat"_s, "noise_2"_s);
			m.Add("LabRat"_s, "noise_3"_s);
			m.Add("LabRat"_s, "noise_4"_s);
			m.Add("LabRat"_s, "noise_5"_s);
			m.NextSet(); // set 60 (1.24) / set 59 (1.23)
			m.Add("Lizard"_s, "noise_1"_s);
			m.Add("Lizard"_s, "noise_2"_s);
			m.Add("Lizard"_s, "noise_3"_s);
			m.Add("Lizard"_s, "noise_4"_s);

			m.NextSet(3, JJ2Version::TSF | JJ2Version::CC);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "die"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_3"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_4"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_5"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_6"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_7"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "hurt_8"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "unknown_mic1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "unknown_mic2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "sidekick"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "fall"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_3"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "jump_4"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "unused_touch"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC, "Lori"_s, "yahoo"_s);

			m.NextSet(3);
			m.Add("UI"_s, "select_1"_s);
			m.Add("UI"_s, "select_2"_s);
			m.Add("UI"_s, "select_3"_s);
			m.Add("UI"_s, "select_4"_s);
			m.Add("UI"_s, "select_5"_s);
			m.Add("UI"_s, "select_6"_s);
			m.Add("UI"_s, "select_7"_s);
			m.Add("UI"_s, "type_char"_s);
			m.Add("UI"_s, "type_enter"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Monkey"_s, "banana_splat"_s);
				m.Add("Monkey"_s, "banana_throw"_s);
				m.NextSet();
				m.Add("Moth"_s, "flap"_s);
			}

			m.NextSet();
			//m.Add("Cinematics"_s, "orangegames_1_boom_l"_s);
			//m.Add("Cinematics"_s, "orangegames_1_boom_r"_s);
			//m.Add("Cinematics"_s, "orangegames_7_bubble_l"_s);
			//m.Add("Cinematics"_s, "orangegames_7_bubble_r"_s);
			//m.Add("Cinematics"_s, "orangegames_2_glass_1_l"_s);
			//m.Add("Cinematics"_s, "orangegames_2_glass_1_r"_s);
			//m.Add("Cinematics"_s, "orangegames_5_glass_2_l"_s);
			//m.Add("Cinematics"_s, "orangegames_5_glass_2_r"_s);
			//m.Add("Cinematics"_s, "orangegames_6_merge"_s);
			//m.Add("Cinematics"_s, "orangegames_3_sweep_1_l"_s);
			//m.Add("Cinematics"_s, "orangegames_3_sweep_1_r"_s);
			//m.Add("Cinematics"_s, "orangegames_4_sweep_2_l"_s);
			//m.Add("Cinematics"_s, "orangegames_4_sweep_2_r"_s);
			//m.Add("Cinematics"_s, "orangegames_5_sweep_3_l"_s);
			//m.Add("Cinematics"_s, "orangegames_5_sweep_3_r"_s);
			m.DiscardItems(15);
			m.NextSet(); // set 70 (1.24) / set 66 (1.23)
			//m.Add("Cinematics"_s, "project2_unused_crunch"_s);
			//m.Add("Cinematics"_s, "project2_10_fart"_s);
			//m.Add("Cinematics"_s, "project2_unused_foew1"_s);
			//m.Add("Cinematics"_s, "project2_unused_foew4"_s);
			//m.Add("Cinematics"_s, "project2_unused_foew5"_s);
			//m.Add("Cinematics"_s, "project2_unused_frog1"_s);
			//m.Add("Cinematics"_s, "project2_unused_frog2"_s);
			//m.Add("Cinematics"_s, "project2_unused_frog3"_s);
			//m.Add("Cinematics"_s, "project2_unused_frog4"_s);
			//m.Add("Cinematics"_s, "project2_unused_frog5"_s);
			//m.Add("Cinematics"_s, "project2_unused_kiss4"_s);
			//m.Add("Cinematics"_s, "project2_unused_open"_s);
			//m.Add("Cinematics"_s, "project2_unused_pinch1"_s);
			//m.Add("Cinematics"_s, "project2_unused_pinch2"_s);
			//m.Add("Cinematics"_s, "project2_3_plop_1"_s);
			//m.Add("Cinematics"_s, "project2_4_plop_2"_s);
			//m.Add("Cinematics"_s, "project2_5_plop_3"_s);
			//m.Add("Cinematics"_s, "project2_6_plop_4"_s);
			//m.Add("Cinematics"_s, "project2_7_plop_5"_s);
			//m.Add("Cinematics"_s, "project2_9_spit"_s);
			//m.Add("Cinematics"_s, "project2_unused_splout"_s);
			//m.Add("Cinematics"_s, "project2_2_splat"_s);
			//m.Add("Cinematics"_s, "project2_1_8_throw"_s);
			//m.Add("Cinematics"_s, "project2_unused_tong"_s);
			m.DiscardItems(24);
			m.NextSet();
			m.Add("Object"_s, "checkpoint_open"_s);
			m.Add("Object"_s, "copter"_s);
			m.Add("Unknown"_s, "unknown_pickup_stretch1a"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Pinball"_s, "bumper_hit"_s);
				m.Add("Pinball"_s, "paddle_hit_1"_s);
				m.Add("Pinball"_s, "paddle_hit_2"_s);
				m.Add("Pinball"_s, "paddle_hit_3"_s);
				m.Add("Pinball"_s, "paddle_hit_4"_s);
				m.NextSet(3);
				m.Add("Queen"_s, "spring"_s);
				m.Add("Queen"_s, "scream"_s);
				m.NextSet();
				m.Add("Rapier"_s, "die"_s);
				m.Add("Rapier"_s, "noise_1"_s);
				m.Add("Rapier"_s, "noise_2"_s);
				m.Add("Rapier"_s, "noise_3"_s);
				m.Add("Rapier"_s, "clunk"_s);
				m.NextSet(2);
				m.Add("Robot"_s, "unknown_big1"_s);
				m.Add("Robot"_s, "unknown_big2"_s);
				m.Add("Robot"_s, "unknown_can1"_s);
				m.Add("Robot"_s, "unknown_can2"_s);
				m.Add("Robot"_s, "attack_start"_s);
				m.Add("Robot"_s, "attack_end"_s);
				m.Add("Robot"_s, "attack"_s);
				m.Add("Robot"_s, "unknown_hydropuf"_s);
				m.Add("Robot"_s, "unknown_idle1"_s);
				m.Add("Robot"_s, "unknown_idle2"_s);
				m.Add("Robot"_s, "unknown_jmpcan1"_s);
				m.Add("Robot"_s, "unknown_jmpcan10"_s);
				m.Add("Robot"_s, "unknown_jmpcan2"_s);
				m.Add("Robot"_s, "unknown_jmpcan3"_s);
				m.Add("Robot"_s, "unknown_jmpcan4"_s);
				m.Add("Robot"_s, "unknown_jmpcan5"_s);
				m.Add("Robot"_s, "unknown_jmpcan6"_s);
				m.Add("Robot"_s, "unknown_jmpcan7"_s);
				m.Add("Robot"_s, "unknown_jmpcan8"_s);
				m.Add("Robot"_s, "unknown_jmpcan9"_s);
				m.Add("Robot"_s, "shrapnel_1"_s);
				m.Add("Robot"_s, "shrapnel_2"_s);
				m.Add("Robot"_s, "shrapnel_3"_s);
				m.Add("Robot"_s, "shrapnel_4"_s);
				m.Add("Robot"_s, "shrapnel_5"_s);
				m.Add("Robot"_s, "attack_start_shutter"_s);
				m.Add("Robot"_s, "unknown_out"_s);
				m.Add("Robot"_s, "unknown_poep"_s);
				m.Add("Robot"_s, "unknown_pole"_s);
				m.Add("Robot"_s, "unknown_shoot"_s);
				m.Add("Robot"_s, "walk_1"_s);
				m.Add("Robot"_s, "walk_2"_s);
				m.Add("Robot"_s, "walk_3"_s);
				m.NextSet();
				m.Add("Object"_s, "rolling_rock"_s);
				m.NextSet();
			}

			m.NextSet(); // set 81 (1.24) / set 77 (1.23)

			if (isFull) {
				//m.Add(JJ2Version::BaseGame | JJ2Version::HH, "Unknown"_s, "sugar_rush_heartbeat"_s);
				m.DiscardItems(1, JJ2Version::BaseGame | JJ2Version::HH);
			}

			m.Add("Common"_s, "sugar_rush"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Common"_s, "science_noise"_s);
				m.NextSet();
				m.Add("Skeleton"_s, "bone_1"_s);
				m.Add("Skeleton"_s, "bone_2"_s);
				m.Add("Skeleton"_s, "bone_3"_s);
				m.Add("Skeleton"_s, "bone_4"_s);
				m.Add("Skeleton"_s, "bone_5"_s);
				m.Add("Skeleton"_s, "bone_6"_s);
			}

			m.NextSet();
			m.Add("Pole"_s, "fall_start"_s);
			m.Add("Pole"_s, "fall_end"_s);
			//m.Add("Pole"_s, "fall_3"_s);
			m.DiscardItems(1);

			if (isFull) {
				m.NextSet(2);
				m.Add("Bolly"_s, "missile_1"_s);
				m.Add("Bolly"_s, "missile_2"_s);
				m.Add("Bolly"_s, "missile_3"_s);
				m.Add("Bolly"_s, "noise"_s);
				m.Add("Bolly"_s, "lock_on"_s);
				m.NextSet(3);
			}

			m.NextSet(3); // set 92 (1.24) / set 88 (1.23)
			m.Add("Spaz"_s, "hurt_1"_s);
			m.Add("Spaz"_s, "hurt_2"_s);
			m.Add("Spaz"_s, "idle_flavor_3_bird_land"_s);
			m.Add("Spaz"_s, "idle_flavor_4"_s);
			m.Add("Spaz"_s, "idle_flavor_3_bird"_s);
			m.Add("Spaz"_s, "idle_flavor_3_eat"_s);
			m.Add("Spaz"_s, "jump_1"_s);
			m.Add("Spaz"_s, "jump_2"_s);
			m.Add("Spaz"_s, "idle_flavor_2"_s);
			m.Add("Spaz"_s, "hihi"_s);
			m.Add("Spaz"_s, "spring_1"_s);
			m.Add("Spaz"_s, "double_jump"_s);
			m.Add("Spaz"_s, "sidekick_1"_s);
			m.Add("Spaz"_s, "sidekick_2"_s);
			m.Add("Spaz"_s, "spring_2"_s);
			m.Add("Spaz"_s, "oooh"_s);
			m.Add("Spaz"_s, "ledge"_s);
			m.Add("Spaz"_s, "jump_3"_s);
			m.Add("Spaz"_s, "jump_4"_s);

			if (isFull) {
				m.NextSet(3);
			}

			m.NextSet();
			m.Add("Spring"_s, "spring_ver_down"_s);
			m.Add("Spring"_s, "spring"_s);
			m.NextSet();
			m.Add("Common"_s, "steam_note"_s);

			if (isFull) {
				m.NextSet();
				m.Add("Unimplemented"_s, "dizzy"_s);
			}

			m.NextSet();
			m.Add("Sucker"_s, "deflate"_s);
			m.Add("Sucker"_s, "pinch_1"_s);
			m.Add("Sucker"_s, "pinch_2"_s);
			m.Add("Sucker"_s, "pinch_3"_s);
			m.Add("Sucker"_s, "plop_1"_s);
			m.Add("Sucker"_s, "plop_2"_s);
			m.Add("Sucker"_s, "plop_3"_s);
			m.Add("Sucker"_s, "plop_4"_s);
			m.Add("Sucker"_s, "up"_s);

			if (isFull) {
				m.NextSet(2); // set 101 (1.24) / set 97 (1.23)
				m.Add("TurtleBoss"_s, "attack_start"_s);
				m.Add("TurtleBoss"_s, "attack_end"_s);
				m.Add("TurtleBoss"_s, "mace"_s);
				m.NextSet();
			}

			m.NextSet();
			m.Add("Turtle"_s, "attack_bite"_s);
			m.Add("Turtle"_s, "turn_start"_s);
			m.Add("Turtle"_s, "shell_collide"_s);
			m.Add("Turtle"_s, "idle_1"_s);
			m.Add("Turtle"_s, "idle_2"_s);
			m.Add("Turtle"_s, "attack_neck"_s);
			m.Add("Turtle"_s, "noise_1"_s);
			m.Add("Turtle"_s, "noise_2"_s);
			m.Add("Turtle"_s, "noise_3"_s);
			m.Add("Turtle"_s, "noise_4"_s);
			m.Add("Turtle"_s, "turn_end"_s);

			if (isFull) {
				m.NextSet(2);
				m.Add("Uterus"_s, "closed_start"_s);
				m.Add("Uterus"_s, "closed_end"_s);
				m.Add("Crab"_s, "noise_1"_s);
				m.Add("Crab"_s, "noise_2"_s);
				m.Add("Crab"_s, "noise_3"_s);
				m.Add("Crab"_s, "noise_4"_s);
				m.Add("Crab"_s, "noise_5"_s);
				m.Add("Crab"_s, "noise_6"_s);
				m.Add("Crab"_s, "noise_7"_s);
				m.Add("Crab"_s, "noise_8"_s);
				m.Add("Uterus"_s, "scream"_s);
				m.Add("Crab"_s, "step_1"_s);
				m.Add("Crab"_s, "step_2"_s);
				m.NextSet(4);
			}

			m.NextSet(2); // set 111 (1.24) / set 107 (1.23)
			m.Add("Common"_s, "wind"_s);
			m.NextSet();
			m.Add("Witch"_s, "laugh"_s);
			m.Add("Witch"_s, "magic"_s);

			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_appear_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_snap"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_throw_fireball"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_fire_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_scary"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_thunder"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Bilsy"_s, "xmas_appear_1"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_noise_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_noise_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_noise_3"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Lizard"_s, "xmas_noise_4"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_attack_bite"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_turn_start"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_shell_collide"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_idle_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_idle_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_attack_neck"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_noise_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_noise_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_noise_3"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_noise_4"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Turtle"_s, "xmas_turn_end"_s);
			m.NextSet(1, JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Doggy"_s, "xmas_attack"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Doggy"_s, "xmas_noise"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Doggy"_s, "xmas_woof_1"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Doggy"_s, "xmas_woof_2"_s);
			m.Add(JJ2Version::TSF | JJ2Version::CC | JJ2Version::HH, "Doggy"_s, "xmas_woof_3"_s);
		}

		return m;
	}

	void AnimSetMapping::DiscardItems(std::uint32_t advanceBy, JJ2Version appliesTo)
	{
		if ((_version & appliesTo) != JJ2Version::Unknown) {
			for (std::uint32_t i = 0; i < advanceBy; i++) {
				Entry entry;
				entry.Category = Discard;

				std::int32_t key = (_currentSet << 16) | _currentItem;
				_entries.emplace(key, std::move(entry));

				_currentItem++;
				_currentOrdinal++;
			}
		}
	}

	void AnimSetMapping::SkipItems(std::uint32_t advanceBy)
	{
		_currentItem += advanceBy;
		_currentOrdinal += advanceBy;
	}

	void AnimSetMapping::NextSet(std::uint32_t advanceBy, JJ2Version appliesTo)
	{
		if ((_version & appliesTo) != JJ2Version::Unknown) {
			_currentSet += advanceBy;
			_currentItem = 0;
		}
	}

	void AnimSetMapping::Add(JJ2Version appliesTo, const StringView& category, const StringView& name, JJ2DefaultPalette palette, bool skipNormalMap, bool allowRealtimePalette)
	{
		if ((_version & appliesTo) != JJ2Version::Unknown) {
			Entry entry;
			entry.Category = category;
			entry.Name = name;
			entry.Ordinal = _currentOrdinal;
			entry.Palette = palette;
			entry.SkipNormalMap = skipNormalMap;
			entry.AllowRealtimePalette = allowRealtimePalette;

			std::uint32_t key = (_currentSet << 16) | _currentItem;
			_entries.emplace(key, std::move(entry));

			_currentItem++;
			_currentOrdinal++;
		}
	}

	void AnimSetMapping::Add(const StringView& category, const StringView& name, JJ2DefaultPalette palette, bool skipNormalMap, bool allowRealtimePalette)
	{
		Entry entry;
		entry.Category = category;
		entry.Name = name;
		entry.Ordinal = _currentOrdinal;
		entry.Palette = palette;
		entry.SkipNormalMap = skipNormalMap;
		entry.AllowRealtimePalette = allowRealtimePalette;

		std::uint32_t key = (_currentSet << 16) | _currentItem;
		_entries.emplace(key, std::move(entry));

		_currentItem++;
		_currentOrdinal++;
	}

	AnimSetMapping::Entry* AnimSetMapping::Get(std::uint32_t set, std::uint32_t item)
	{
		if (set > UINT16_MAX || item > UINT16_MAX) {
			return nullptr;
		}

		std::uint32_t key = (set << 16) | item;
		auto it = _entries.find(key);
		if (it != _entries.end()) {
			return &it->second;
		} else {
			return nullptr;
		}
	}

	AnimSetMapping::Entry* AnimSetMapping::GetByOrdinal(std::uint32_t index)
	{
		for (auto& [key, entry] : _entries) {
			if (entry.Ordinal == index) {
				return &entry;
			}
		}

		return nullptr;
	}
}