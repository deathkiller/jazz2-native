#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	/** @brief Food type */
	enum class FoodType
	{
		Apple = 1,			/**< Apple */
		Banana = 2,			/**< Banana */
		Cherry = 3,			/**< Cherry */
		Orange = 4,			/**< Orange */
		Pear = 5,			/**< Pear */
		Pretzel = 6,		/**< Pretzel */
		Strawberry = 7,		/**< Strawberry */
		Lemon = 8,			/**< Lemon */
		Lime = 9,			/**< Lime */
		Thing = 10,			/**< Thing */
		WaterMelon = 11,	/**< Watermelon */
		Peach = 12,			/**< Peach */
		Grapes = 13,		/**< Grapes */
		Lettuce = 14,		/**< Lettuce */
		Eggplant = 15,		/**< Eggplant */
		Cucumber = 16,		/**< Cucumber */
		Pepsi = 17,			/**< Pepsi */
		Coke = 18,			/**< Coke */
		Milk = 19,			/**< Milk */
		Pie = 20,			/**< Pie */
		Cake = 21,			/**< Cake */
		Donut = 22,			/**< Donut */
		Cupcake = 23,		/**< Cupcake */
		Chips = 24,			/**< Chips */
		Candy = 25,			/**< Candy */
		Chocolate = 26,		/**< Chocolate */
		IceCream = 27,		/**< Ice cream */
		Burger = 28,		/**< Burger */
		Pizza = 29,			/**< Pizza */
		Fries = 30,			/**< Fries */
		ChickenLeg = 31,	/**< Chicken leg */
		Sandwich = 32,		/**< Sandwich */
		Taco = 33,			/**< Taco */
		HotDog = 34,		/**< Hot dog */
		Ham = 35,			/**< Ham */
		Cheese = 36,		/**< Cheese */
	};

	/**
		@brief Food (collectible)
		
		The assorted food and drink items scattered throughout JJ2 levels (fruit, sweets, fast food, sodas,
		etc.). Each item awards a small amount of points and counts toward the player's food tally; eating
		enough food triggers the Sugar Rush power-up.
	*/
	class FoodCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		/** @brief Creates a new instance */
		FoodCollectible();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		bool _isDrinkable;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}