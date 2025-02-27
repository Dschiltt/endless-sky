/* Planet.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "Planet.h"

#include "DataNode.h"
#include "text/Format.h"
#include "GameData.h"
#include "Government.h"
#include "PlayerInfo.h"
#include "Politics.h"
#include "Random.h"
#include "Ship.h"
#include "ShipEvent.h"
#include "SpriteSet.h"
#include "System.h"

#include <algorithm>

using namespace std;

namespace {
	const string WORMHOLE = "wormhole";
	const string PLANET = "planet";

	// Planet attributes in the form "requires: <attribute>" restrict the ability of ships to land
	// unless the ship has all required attributes.
	void SetRequiredAttributes(const set<string> &attributes, set<string> &required)
	{
		static const string PREFIX = "requires: ";
		static const string PREFIX_END = "requires:!";
		required.clear();
		for_each(attributes.lower_bound(PREFIX), attributes.lower_bound(PREFIX_END), [&](const string &attribute)
		{
			required.emplace_hint(required.cend(), attribute.substr(PREFIX.length()));
		});
	}
}



// Load a planet's description from a file.
void Planet::Load(const DataNode &node)
{
	if(node.Size() < 2)
		return;
	name = node.Token(1);
	// The planet's name is needed to save references to this object, so a
	// flag is used to test whether Load() was called at least once for it.
	isDefined = true;

	// If this planet has been loaded before, these sets of items should be
	// reset instead of appending to them:
	set<string> shouldOverwrite = {"attributes", "description", "spaceport"};

	for(const DataNode &child : node)
	{
		// Check for the "add" or "remove" keyword.
		bool add = (child.Token(0) == "add");
		bool remove = (child.Token(0) == "remove");
		if((add || remove) && child.Size() < 2)
		{
			child.PrintTrace("Skipping " + child.Token(0) + " with no key given:");
			continue;
		}

		// Get the key and value (if any).
		const string &key = child.Token((add || remove) ? 1 : 0);
		int valueIndex = (add || remove) ? 2 : 1;
		bool hasValue = (child.Size() > valueIndex);
		const string &value = child.Token(hasValue ? valueIndex : 0);

		// Check for conditions that require clearing this key's current value.
		// "remove <key>" means to clear the key's previous contents.
		// "remove <key> <value>" means to remove just that value from the key.
		bool removeAll = (remove && !hasValue);
		// "<key> clear" is the deprecated way of writing "remove <key>."
		removeAll |= (!add && !remove && hasValue && value == "clear");
		// If this is the first entry for the given key, and we are not in "add"
		// or "remove" mode, its previous value should be cleared.
		bool overwriteAll = (!add && !remove && !removeAll && shouldOverwrite.count(key));
		// Clear the data of the given type.
		if(removeAll || overwriteAll)
		{
			// Clear the data of the given type.
			if(key == "music")
				music.clear();
			else if(key == "attributes")
				attributes.clear();
			else if(key == "description")
				description.clear();
			else if(key == "spaceport")
				spaceport.clear();
			else if(key == "shipyard")
				shipSales.clear();
			else if(key == "outfitter")
				outfitSales.clear();
			else if(key == "government")
				government = nullptr;
			else if(key == "required reputation")
				requiredReputation = 0.;
			else if(key == "bribe")
				bribe = 0.;
			else if(key == "security")
				security = 0.;
			else if(key == "tribute")
				tribute = 0;

			// If not in "overwrite" mode, move on to the next node.
			if(overwriteAll)
				shouldOverwrite.erase(key);
			else
				continue;
		}

		// Handle the attributes which can be "removed."
		if(!hasValue)
		{
			child.PrintTrace("Error: Expected key to have a value:");
			continue;
		}
		else if(key == "attributes")
		{
			if(remove)
				for(int i = valueIndex; i < child.Size(); ++i)
					attributes.erase(child.Token(i));
			else
				for(int i = valueIndex; i < child.Size(); ++i)
					attributes.insert(child.Token(i));
		}
		else if(key == "shipyard")
		{
			if(remove)
				shipSales.erase(GameData::Shipyards().Get(value));
			else
				shipSales.insert(GameData::Shipyards().Get(value));
		}
		else if(key == "outfitter")
		{
			if(remove)
				outfitSales.erase(GameData::Outfitters().Get(value));
			else
				outfitSales.insert(GameData::Outfitters().Get(value));
		}
		// Handle the attributes which cannot be "removed."
		else if(remove)
		{
			child.PrintTrace("Error: Cannot \"remove\" a specific value from the given key:");
			continue;
		}
		else if(key == "landscape")
			landscape = SpriteSet::Get(value);
		else if(key == "music")
			music = value;
		else if(key == "description" || key == "spaceport")
		{
			string &text = (key == "description") ? description : spaceport;
			if(!text.empty() && !value.empty() && value[0] > ' ')
				text += '\t';
			text += value;
			text += '\n';
		}
		else if(key == "government")
			government = GameData::Governments().Get(value);
		else if(key == "required reputation")
			requiredReputation = child.Value(valueIndex);
		else if(key == "bribe")
			bribe = child.Value(valueIndex);
		else if(key == "security")
		{
			customSecurity = true;
			security = child.Value(valueIndex);
		}
		else if(key == "tribute")
		{
			tribute = child.Value(valueIndex);
			bool resetFleets = !defenseFleets.empty();
			for(const DataNode &grand : child)
			{
				if(grand.Token(0) == "threshold" && grand.Size() >= 2)
					defenseThreshold = grand.Value(1);
				else if(grand.Token(0) == "fleet")
				{
					if(grand.Size() >= 2 && !grand.HasChildren())
					{
						// Allow only one "tribute" node to define the tribute fleets.
						if(resetFleets)
						{
							defenseFleets.clear();
							resetFleets = false;
						}
						defenseFleets.insert(defenseFleets.end(),
								grand.Size() >= 3 ? grand.Value(2) : 1,
								GameData::Fleets().Get(grand.Token(1))
						);
					}
					else
						grand.PrintTrace("Skipping unsupported tribute fleet definition:");
				}
				else
					grand.PrintTrace("Skipping unrecognized tribute attribute:");
			}
		}
		else
			child.PrintTrace("Skipping unrecognized attribute:");
	}

	static const vector<string> AUTO_ATTRIBUTES = {"spaceport", "shipyard", "outfitter"};
	bool autoValues[3] = {!spaceport.empty(), !shipSales.empty(), !outfitSales.empty()};
	for(unsigned i = 0; i < AUTO_ATTRIBUTES.size(); ++i)
	{
		if(autoValues[i])
			attributes.insert(AUTO_ATTRIBUTES[i]);
		else
			attributes.erase(AUTO_ATTRIBUTES[i]);
	}

	// Precalculate commonly used values that can only change due to Load().
	inhabited = (HasSpaceport() || requiredReputation || !defenseFleets.empty()) && !attributes.count("uninhabited");
	SetRequiredAttributes(Attributes(), requiredAttributes);
}



// Test if this planet has been loaded (vs. just referred to). It must also be located in
// at least one system, and all systems that claim it must themselves be valid.
bool Planet::IsValid() const
{
	return isDefined && !systems.empty() && all_of(systems.begin(), systems.end(),
		[](const System *s) noexcept -> bool { return s->IsValid(); });
}



// Get the name of the planet.
const string &Planet::Name() const
{
	static const string UNKNOWN = "???";
	if(IsWormhole())
		return UNKNOWN;

	return name;
}



void Planet::SetName(const string &name)
{
	this->name = name;
}



// Get the name used for this planet in the data files.
const string &Planet::TrueName() const
{
	return name;
}



// Get the planet's descriptive text.
const string &Planet::Description() const
{
	return description;
}



// Get the landscape sprite.
const Sprite *Planet::Landscape() const
{
	return landscape;
}



// Get the name of the ambient audio to play on this planet.
const string &Planet::MusicName() const
{
	return music;
}



// Get the list of "attributes" of the planet.
const set<string> &Planet::Attributes() const
{
	return attributes;
}



// Get planet's noun descriptor from attributes
const string &Planet::Noun() const
{
	if(IsWormhole())
		return WORMHOLE;

	for(const string &attribute : attributes)
		if(attribute == "moon" || attribute == "station")
			return attribute;

	return PLANET;
}



// Check whether there is a spaceport (which implies there is also trading,
// jobs, banking, and hiring).
bool Planet::HasSpaceport() const
{
	return !spaceport.empty();
}



// Get the spaceport's descriptive text.
const string &Planet::SpaceportDescription() const
{
	return spaceport;
}



// Check if this planet is inhabited (i.e. it has a spaceport, and does not
// have the "uninhabited" attribute).
bool Planet::IsInhabited() const
{
	return inhabited;
}



// Check if this planet has a shipyard.
bool Planet::HasShipyard() const
{
	return !Shipyard().empty();
}



// Get the list of ships in the shipyard.
const Sale<Ship> &Planet::Shipyard() const
{
	shipyard.clear();
	for(const Sale<Ship> *sale : shipSales)
		shipyard.Add(*sale);

	return shipyard;
}



// Check if this planet has an outfitter.
bool Planet::HasOutfitter() const
{
	return !Outfitter().empty();
}



// Get the list of outfits available from the outfitter.
const Sale<Outfit> &Planet::Outfitter() const
{
	outfitter.clear();
	for(const Sale<Outfit> *sale : outfitSales)
		outfitter.Add(*sale);

	return outfitter;
}



// Get this planet's government. Most planets follow the government of the system they are in.
const Government *Planet::GetGovernment() const
{
	return government ? government : systems.empty() ? nullptr : GetSystem()->GetGovernment();
}



// You need this good a reputation with this system's government to land here.
double Planet::RequiredReputation() const
{
	return requiredReputation;
}



// This is what fraction of your fleet's value you must pay as a bribe in
// order to land on this planet. (If zero, you cannot bribe it.)
double Planet::GetBribeFraction() const
{
	return bribe;
}



// This is how likely the planet's authorities are to notice if you are
// doing something illegal.
double Planet::Security() const
{
	return security;
}



bool Planet::HasCustomSecurity() const
{
	return customSecurity;
}



const System *Planet::GetSystem() const
{
	return (systems.empty() ? nullptr : systems.front());
}



// Check if this planet is in the given system. Note that wormholes may be
// in more than one system.
bool Planet::IsInSystem(const System *system) const
{
	return (find(systems.begin(), systems.end(), system) != systems.end());
}



void Planet::SetSystem(const System *system)
{
	if(find(systems.begin(), systems.end(), system) == systems.end())
		systems.push_back(system);
}



// Remove the given system from the list of systems this planet is in. This
// must be done when game events rearrange the planets in a system.
void Planet::RemoveSystem(const System *system)
{
	auto it = find(systems.begin(), systems.end(), system);
	if(it != systems.end())
		systems.erase(it);
}



// Check if this is a wormhole (that is, it appears in multiple systems).
bool Planet::IsWormhole() const
{
	return (systems.size() > 1);
}



const System *Planet::WormholeSource(const System *to) const
{
	auto it = find(systems.begin(), systems.end(), to);
	if(it == systems.end())
		return to;

	return (it == systems.begin() ? systems.back() : *--it);
}




const System *Planet::WormholeDestination(const System *from) const
{
	auto it = find(systems.begin(), systems.end(), from);
	if(it == systems.end())
		return from;

	++it;
	return (it == systems.end() ? systems.front() : *it);
}



const vector<const System *> &Planet::WormholeSystems() const
{
	return systems;
}



// Check if the given ship has all the attributes necessary to allow it to
// land on this planet.
bool Planet::IsAccessible(const Ship *ship) const
{
	// If there are no required attributes, then any ship may land here.
	if(IsUnrestricted())
		return true;
	if(!ship)
		return false;

	const auto &shipAttributes = ship->Attributes();
	return all_of(requiredAttributes.cbegin(), requiredAttributes.cend(),
			[&](const string &attr) -> bool { return shipAttributes.Get(attr); });
}



// Check if this planet has any required attributes that restrict landability.
bool Planet::IsUnrestricted() const
{
	return requiredAttributes.empty();
}



// Below are convenience functions which access the game state in Politics,
// but do so with a less convoluted syntax:
bool Planet::HasFuelFor(const Ship &ship) const
{
	return !IsWormhole() && HasSpaceport() && CanLand(ship);
}



bool Planet::CanLand(const Ship &ship) const
{
	return IsAccessible(&ship) && GameData::GetPolitics().CanLand(ship, this);
}



bool Planet::CanLand() const
{
	return GameData::GetPolitics().CanLand(this);
}



bool Planet::CanUseServices() const
{
	return GameData::GetPolitics().CanUseServices(this);
}



void Planet::Bribe(bool fullAccess) const
{
	GameData::GetPolitics().BribePlanet(this, fullAccess);
}



// Demand tribute, and get the planet's response.
string Planet::DemandTribute(PlayerInfo &player) const
{
	if(player.GetCondition("tribute: " + name))
		return "We are already paying you as much as we can afford.";
	if(!tribute || defenseFleets.empty())
		return "Please don't joke about that sort of thing.";
	if(player.GetCondition("combat rating") < defenseThreshold)
		return "You're not worthy of our time.";

	// The player is scary enough for this planet to take notice. Check whether
	// this is the first demand for tribute, or not.
	if(!isDefending)
	{
		isDefending = true;
		set<const Government *> toProvoke;
		for(const auto &fleet : defenseFleets)
			toProvoke.insert(fleet->GetGovernment());
		for(const auto &gov : toProvoke)
			gov->Offend(ShipEvent::PROVOKE);
		// Terrorizing a planet is not taken lightly by it or its allies.
		// TODO: Use a distinct event type for the domination system and
		// expose syntax for controlling its impact on the targeted government
		// and those that know it.
		GetGovernment()->Offend(ShipEvent::ATROCITY);
		return "Our defense fleet will make short work of you.";
	}

	// The player has already demanded tribute. Have they defeated the entire defense fleet?
	bool isDefeated = (defenseDeployed == defenseFleets.size());
	for(const shared_ptr<Ship> &ship : defenders)
		if(!ship->IsDisabled() && !ship->IsYours())
		{
			isDefeated = false;
			break;
		}

	if(!isDefeated)
		return "We're not ready to surrender yet.";

	player.Conditions()["tribute: " + name] = tribute;
	GameData::GetPolitics().DominatePlanet(this);
	return "We surrender. We will pay you " + Format::Credits(tribute) + " credits per day to leave us alone.";
}



// While being tributed, attempt to spawn the next specified defense fleet.
void Planet::DeployDefense(list<shared_ptr<Ship>> &ships) const
{
	if(!isDefending || Random::Int(60) || defenseDeployed == defenseFleets.size())
		return;

	auto end = defenders.begin();
	defenseFleets[defenseDeployed]->Enter(*GetSystem(), defenders, this);
	ships.insert(ships.begin(), defenders.begin(), end);

	// All defenders use a special personality.
	Personality defenderPersonality = Personality::Defender();
	Personality fighterPersonality = Personality::DefenderFighter();
	for(auto it = defenders.begin(); it != end; ++it)
	{
		(**it).SetPersonality(defenderPersonality);
		if((**it).HasBays())
			for(auto bay = (**it).Bays().begin(); bay != (**it).Bays().end(); ++bay)
				if(bay->ship)
					bay->ship->SetPersonality(fighterPersonality);
	}

	++defenseDeployed;
}



void Planet::ResetDefense() const
{
	isDefending = false;
	defenseDeployed = 0;
	defenders.clear();
}
