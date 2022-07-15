#include "FoodCollectible.h"
#include "../../LevelInitialization.h"
#include "../Player.h"

namespace Jazz2::Actors::Collectibles
{
	FoodCollectible::FoodCollectible()
		:
		_isDrinkable(false)
	{
	}

	void FoodCollectible::Preload(const ActorActivationDetails& details)
	{
		FoodType foodType = (FoodType)details.Params[0];
		switch (foodType) {
			case FoodType::Apple: PreloadMetadataAsync("Collectible/FoodApple"); break;
			case FoodType::Banana: PreloadMetadataAsync("Collectible/FoodBanana"); break;
			case FoodType::Cherry: PreloadMetadataAsync("Collectible/FoodCherry"); break;
			case FoodType::Orange: PreloadMetadataAsync("Collectible/FoodOrange"); break;
			case FoodType::Pear: PreloadMetadataAsync("Collectible/FoodPear"); break;
			case FoodType::Pretzel: PreloadMetadataAsync("Collectible/FoodPretzel"); break;
			case FoodType::Strawberry: PreloadMetadataAsync("Collectible/FoodStrawberry"); break;
			case FoodType::Lemon: PreloadMetadataAsync("Collectible/FoodLemon"); break;
			case FoodType::Lime: PreloadMetadataAsync("Collectible/FoodLime"); break;
			case FoodType::Thing: PreloadMetadataAsync("Collectible/FoodThing"); break;
			case FoodType::WaterMelon: PreloadMetadataAsync("Collectible/FoodWaterMelon"); break;
			case FoodType::Peach: PreloadMetadataAsync("Collectible/FoodPeach"); break;
			case FoodType::Grapes: PreloadMetadataAsync("Collectible/FoodGrapes"); break;
			case FoodType::Lettuce: PreloadMetadataAsync("Collectible/FoodLettuce"); break;
			case FoodType::Eggplant: PreloadMetadataAsync("Collectible/FoodEggplant"); break;
			case FoodType::Cucumber: PreloadMetadataAsync("Collectible/FoodCucumber"); break;
			case FoodType::Pepsi: PreloadMetadataAsync("Collectible/FoodPepsi"); break;
			case FoodType::Coke: PreloadMetadataAsync("Collectible/FoodCoke"); break;
			case FoodType::Milk: PreloadMetadataAsync("Collectible/FoodMilk"); break;
			case FoodType::Pie: PreloadMetadataAsync("Collectible/FoodPie"); break;
			case FoodType::Cake: PreloadMetadataAsync("Collectible/FoodCake"); break;
			case FoodType::Donut: PreloadMetadataAsync("Collectible/FoodDonut"); break;
			case FoodType::Cupcake: PreloadMetadataAsync("Collectible/FoodCupcake"); break;
			case FoodType::Chips: PreloadMetadataAsync("Collectible/FoodChips"); break;
			case FoodType::Candy: PreloadMetadataAsync("Collectible/FoodCandy"); break;
			case FoodType::Chocolate: PreloadMetadataAsync("Collectible/FoodChocolate"); break;
			case FoodType::IceCream: PreloadMetadataAsync("Collectible/FoodIceCream"); break;
			case FoodType::Burger: PreloadMetadataAsync("Collectible/FoodBurger"); break;
			case FoodType::Pizza: PreloadMetadataAsync("Collectible/FoodPizza"); break;
			case FoodType::Fries: PreloadMetadataAsync("Collectible/FoodFries"); break;
			case FoodType::ChickenLeg: PreloadMetadataAsync("Collectible/FoodChickenLeg"); break;
			case FoodType::Sandwich: PreloadMetadataAsync("Collectible/FoodSandwich"); break;
			case FoodType::Taco: PreloadMetadataAsync("Collectible/FoodTaco"); break;
			case FoodType::HotDog: PreloadMetadataAsync("Collectible/FoodHotDog"); break;
			case FoodType::Ham: PreloadMetadataAsync("Collectible/FoodHam"); break;
			case FoodType::Cheese: PreloadMetadataAsync("Collectible/FoodCheese"); break;
		}
	}

	Task<bool> FoodCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 50;

		FoodType foodType = (FoodType)details.Params[0];
		switch (foodType) {
			case FoodType::Apple: co_await RequestMetadataAsync("Collectible/FoodApple"); break;
			case FoodType::Banana: co_await RequestMetadataAsync("Collectible/FoodBanana"); break;
			case FoodType::Cherry: co_await RequestMetadataAsync("Collectible/FoodCherry"); break;
			case FoodType::Orange: co_await RequestMetadataAsync("Collectible/FoodOrange"); break;
			case FoodType::Pear: co_await RequestMetadataAsync("Collectible/FoodPear"); break;
			case FoodType::Pretzel: co_await RequestMetadataAsync("Collectible/FoodPretzel"); break;
			case FoodType::Strawberry: co_await RequestMetadataAsync("Collectible/FoodStrawberry"); break;
			case FoodType::Lemon: co_await RequestMetadataAsync("Collectible/FoodLemon"); break;
			case FoodType::Lime: co_await RequestMetadataAsync("Collectible/FoodLime"); break;
			case FoodType::Thing: co_await RequestMetadataAsync("Collectible/FoodThing"); break;
			case FoodType::WaterMelon: co_await RequestMetadataAsync("Collectible/FoodWaterMelon"); break;
			case FoodType::Peach: co_await RequestMetadataAsync("Collectible/FoodPeach"); break;
			case FoodType::Grapes: co_await RequestMetadataAsync("Collectible/FoodGrapes"); break;
			case FoodType::Lettuce: co_await RequestMetadataAsync("Collectible/FoodLettuce"); break;
			case FoodType::Eggplant: co_await RequestMetadataAsync("Collectible/FoodEggplant"); break;
			case FoodType::Cucumber: co_await RequestMetadataAsync("Collectible/FoodCucumber"); break;
			case FoodType::Pepsi: co_await RequestMetadataAsync("Collectible/FoodPepsi"); break;
			case FoodType::Coke: co_await RequestMetadataAsync("Collectible/FoodCoke"); break;
			case FoodType::Milk: co_await RequestMetadataAsync("Collectible/FoodMilk"); break;
			case FoodType::Pie: co_await RequestMetadataAsync("Collectible/FoodPie"); break;
			case FoodType::Cake: co_await RequestMetadataAsync("Collectible/FoodCake"); break;
			case FoodType::Donut: co_await RequestMetadataAsync("Collectible/FoodDonut"); break;
			case FoodType::Cupcake: co_await RequestMetadataAsync("Collectible/FoodCupcake"); break;
			case FoodType::Chips: co_await RequestMetadataAsync("Collectible/FoodChips"); break;
			case FoodType::Candy: co_await RequestMetadataAsync("Collectible/FoodCandy"); break;
			case FoodType::Chocolate: co_await RequestMetadataAsync("Collectible/FoodChocolate"); break;
			case FoodType::IceCream: co_await RequestMetadataAsync("Collectible/FoodIceCream"); break;
			case FoodType::Burger: co_await RequestMetadataAsync("Collectible/FoodBurger"); break;
			case FoodType::Pizza: co_await RequestMetadataAsync("Collectible/FoodPizza"); break;
			case FoodType::Fries: co_await RequestMetadataAsync("Collectible/FoodFries"); break;
			case FoodType::ChickenLeg: co_await RequestMetadataAsync("Collectible/FoodChickenLeg"); break;
			case FoodType::Sandwich: co_await RequestMetadataAsync("Collectible/FoodSandwich"); break;
			case FoodType::Taco: co_await RequestMetadataAsync("Collectible/FoodTaco"); break;
			case FoodType::HotDog: co_await RequestMetadataAsync("Collectible/FoodHotDog"); break;
			case FoodType::Ham: co_await RequestMetadataAsync("Collectible/FoodHam"); break;
			case FoodType::Cheese: co_await RequestMetadataAsync("Collectible/FoodCheese"); break;
		}

		switch (foodType) {
			case FoodType::Pepsi:
			case FoodType::Coke:
			case FoodType::Milk:
				_isDrinkable = true;
				break;
			default:
				_isDrinkable = false;
				break;
		}

		SetAnimation("Food");
		SetFacingDirection();

		co_return true;
	}

	void FoodCollectible::OnCollect(Player* player)
	{
		player->ConsumeFood(_isDrinkable);

		CollectibleBase::OnCollect(player);
	}
}