#pragma once

#include "CollectibleBase.h"

namespace Jazz2::Actors::Collectibles
{
	enum class FoodType {
		Apple = 1,
		Banana = 2,
		Cherry = 3,
		Orange = 4,
		Pear = 5,
		Pretzel = 6,
		Strawberry = 7,
		Lemon = 8,
		Lime = 9,
		Thing = 10,
		WaterMelon = 11,
		Peach = 12,
		Grapes = 13,
		Lettuce = 14,
		Eggplant = 15,
		Cucumber = 16,
		Pepsi = 17,
		Coke = 18,
		Milk = 19,
		Pie = 20,
		Cake = 21,
		Donut = 22,
		Cupcake = 23,
		Chips = 24,
		Candy = 25,
		Chocolate = 26,
		IceCream = 27,
		Burger = 28,
		Pizza = 29,
		Fries = 30,
		ChickenLeg = 31,
		Sandwich = 32,
		Taco = 33,
		HotDog = 34,
		Ham = 35,
		Cheese = 36,
	};

	class FoodCollectible : public CollectibleBase
	{
		DEATH_RUNTIME_OBJECT(CollectibleBase);

	public:
		FoodCollectible();

		static void Preload(const ActorActivationDetails& details);

	protected:
		bool _isDrinkable;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnCollect(Player* player) override;
	};
}