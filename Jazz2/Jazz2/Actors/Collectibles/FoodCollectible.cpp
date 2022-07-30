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
			case FoodType::Apple: PreloadMetadataAsync("Collectible/FoodApple"_s); break;
			case FoodType::Banana: PreloadMetadataAsync("Collectible/FoodBanana"_s); break;
			case FoodType::Cherry: PreloadMetadataAsync("Collectible/FoodCherry"_s); break;
			case FoodType::Orange: PreloadMetadataAsync("Collectible/FoodOrange"_s); break;
			case FoodType::Pear: PreloadMetadataAsync("Collectible/FoodPear"_s); break;
			case FoodType::Pretzel: PreloadMetadataAsync("Collectible/FoodPretzel"_s); break;
			case FoodType::Strawberry: PreloadMetadataAsync("Collectible/FoodStrawberry"_s); break;
			case FoodType::Lemon: PreloadMetadataAsync("Collectible/FoodLemon"_s); break;
			case FoodType::Lime: PreloadMetadataAsync("Collectible/FoodLime"_s); break;
			case FoodType::Thing: PreloadMetadataAsync("Collectible/FoodThing"_s); break;
			case FoodType::WaterMelon: PreloadMetadataAsync("Collectible/FoodWaterMelon"_s); break;
			case FoodType::Peach: PreloadMetadataAsync("Collectible/FoodPeach"_s); break;
			case FoodType::Grapes: PreloadMetadataAsync("Collectible/FoodGrapes"_s); break;
			case FoodType::Lettuce: PreloadMetadataAsync("Collectible/FoodLettuce"_s); break;
			case FoodType::Eggplant: PreloadMetadataAsync("Collectible/FoodEggplant"_s); break;
			case FoodType::Cucumber: PreloadMetadataAsync("Collectible/FoodCucumber"_s); break;
			case FoodType::Pepsi: PreloadMetadataAsync("Collectible/FoodPepsi"_s); break;
			case FoodType::Coke: PreloadMetadataAsync("Collectible/FoodCoke"_s); break;
			case FoodType::Milk: PreloadMetadataAsync("Collectible/FoodMilk"_s); break;
			case FoodType::Pie: PreloadMetadataAsync("Collectible/FoodPie"_s); break;
			case FoodType::Cake: PreloadMetadataAsync("Collectible/FoodCake"_s); break;
			case FoodType::Donut: PreloadMetadataAsync("Collectible/FoodDonut"_s); break;
			case FoodType::Cupcake: PreloadMetadataAsync("Collectible/FoodCupcake"_s); break;
			case FoodType::Chips: PreloadMetadataAsync("Collectible/FoodChips"_s); break;
			case FoodType::Candy: PreloadMetadataAsync("Collectible/FoodCandy"_s); break;
			case FoodType::Chocolate: PreloadMetadataAsync("Collectible/FoodChocolate"_s); break;
			case FoodType::IceCream: PreloadMetadataAsync("Collectible/FoodIceCream"_s); break;
			case FoodType::Burger: PreloadMetadataAsync("Collectible/FoodBurger"_s); break;
			case FoodType::Pizza: PreloadMetadataAsync("Collectible/FoodPizza"_s); break;
			case FoodType::Fries: PreloadMetadataAsync("Collectible/FoodFries"_s); break;
			case FoodType::ChickenLeg: PreloadMetadataAsync("Collectible/FoodChickenLeg"_s); break;
			case FoodType::Sandwich: PreloadMetadataAsync("Collectible/FoodSandwich"_s); break;
			case FoodType::Taco: PreloadMetadataAsync("Collectible/FoodTaco"_s); break;
			case FoodType::HotDog: PreloadMetadataAsync("Collectible/FoodHotDog"_s); break;
			case FoodType::Ham: PreloadMetadataAsync("Collectible/FoodHam"_s); break;
			case FoodType::Cheese: PreloadMetadataAsync("Collectible/FoodCheese"_s); break;
		}
	}

	Task<bool> FoodCollectible::OnActivatedAsync(const ActorActivationDetails& details)
	{
		co_await CollectibleBase::OnActivatedAsync(details);

		_scoreValue = 50;

		FoodType foodType = (FoodType)details.Params[0];
		switch (foodType) {
			case FoodType::Apple: co_await RequestMetadataAsync("Collectible/FoodApple"_s); break;
			case FoodType::Banana: co_await RequestMetadataAsync("Collectible/FoodBanana"_s); break;
			case FoodType::Cherry: co_await RequestMetadataAsync("Collectible/FoodCherry"_s); break;
			case FoodType::Orange: co_await RequestMetadataAsync("Collectible/FoodOrange"_s); break;
			case FoodType::Pear: co_await RequestMetadataAsync("Collectible/FoodPear"_s); break;
			case FoodType::Pretzel: co_await RequestMetadataAsync("Collectible/FoodPretzel"_s); break;
			case FoodType::Strawberry: co_await RequestMetadataAsync("Collectible/FoodStrawberry"_s); break;
			case FoodType::Lemon: co_await RequestMetadataAsync("Collectible/FoodLemon"_s); break;
			case FoodType::Lime: co_await RequestMetadataAsync("Collectible/FoodLime"_s); break;
			case FoodType::Thing: co_await RequestMetadataAsync("Collectible/FoodThing"_s); break;
			case FoodType::WaterMelon: co_await RequestMetadataAsync("Collectible/FoodWaterMelon"_s); break;
			case FoodType::Peach: co_await RequestMetadataAsync("Collectible/FoodPeach"_s); break;
			case FoodType::Grapes: co_await RequestMetadataAsync("Collectible/FoodGrapes"_s); break;
			case FoodType::Lettuce: co_await RequestMetadataAsync("Collectible/FoodLettuce"_s); break;
			case FoodType::Eggplant: co_await RequestMetadataAsync("Collectible/FoodEggplant"_s); break;
			case FoodType::Cucumber: co_await RequestMetadataAsync("Collectible/FoodCucumber"_s); break;
			case FoodType::Pepsi: co_await RequestMetadataAsync("Collectible/FoodPepsi"_s); break;
			case FoodType::Coke: co_await RequestMetadataAsync("Collectible/FoodCoke"_s); break;
			case FoodType::Milk: co_await RequestMetadataAsync("Collectible/FoodMilk"_s); break;
			case FoodType::Pie: co_await RequestMetadataAsync("Collectible/FoodPie"_s); break;
			case FoodType::Cake: co_await RequestMetadataAsync("Collectible/FoodCake"_s); break;
			case FoodType::Donut: co_await RequestMetadataAsync("Collectible/FoodDonut"_s); break;
			case FoodType::Cupcake: co_await RequestMetadataAsync("Collectible/FoodCupcake"_s); break;
			case FoodType::Chips: co_await RequestMetadataAsync("Collectible/FoodChips"_s); break;
			case FoodType::Candy: co_await RequestMetadataAsync("Collectible/FoodCandy"_s); break;
			case FoodType::Chocolate: co_await RequestMetadataAsync("Collectible/FoodChocolate"_s); break;
			case FoodType::IceCream: co_await RequestMetadataAsync("Collectible/FoodIceCream"_s); break;
			case FoodType::Burger: co_await RequestMetadataAsync("Collectible/FoodBurger"_s); break;
			case FoodType::Pizza: co_await RequestMetadataAsync("Collectible/FoodPizza"_s); break;
			case FoodType::Fries: co_await RequestMetadataAsync("Collectible/FoodFries"_s); break;
			case FoodType::ChickenLeg: co_await RequestMetadataAsync("Collectible/FoodChickenLeg"_s); break;
			case FoodType::Sandwich: co_await RequestMetadataAsync("Collectible/FoodSandwich"_s); break;
			case FoodType::Taco: co_await RequestMetadataAsync("Collectible/FoodTaco"_s); break;
			case FoodType::HotDog: co_await RequestMetadataAsync("Collectible/FoodHotDog"_s); break;
			case FoodType::Ham: co_await RequestMetadataAsync("Collectible/FoodHam"_s); break;
			case FoodType::Cheese: co_await RequestMetadataAsync("Collectible/FoodCheese"_s); break;
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

		SetAnimation("Food"_s);
		SetFacingDirection();

		co_return true;
	}

	void FoodCollectible::OnCollect(Player* player)
	{
		player->ConsumeFood(_isDrinkable);

		CollectibleBase::OnCollect(player);
	}
}