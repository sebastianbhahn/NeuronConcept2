#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <map>
#include <tuple>
#include <set>
#include <random>


std::unordered_map<std::string, synapse> synapseMap;
std::unordered_map<std::string, neuron> neuronMap;

enum class tickClock { uptick, downtick };

tickClock mainClock = tickClock::downtick;

void tick() {
	mainClock = static_cast<tickClock>((static_cast<int>(mainClock) + 1) % 2);
}


int rewardValue = 0;
bool rewardNeuronExists = false;

struct cellPosition {
	long x, y, z;
};

std::set<cellPosition> occupiedCellPositions;

bool cellPosOccupied(const cellPosition& pos) {
	return occupiedCellPositions.find(pos) != occupiedCellPositions.end();
}


struct neuronPosition {
	cellPosition Position;
	std::string hash;
};

std::mutex firedNeuronListMute;
std::vector<neuronPosition> firedNeuronList;

void pushToFiredNeuronList(const neuronPosition& posData) {
	std::lock_guard<std::mutex> lock(firedNeuronListMute);
	firedNeuronList.push_back(posData);
}

class synapse {
public:
	std::string hash;
	cellPosition parentNeuron;
	cellPosition childNeuron;
	int strength = 1;
	bool isCharged = false;

	void rewardSynapses(bool reward, const int& amount) {
		if (strength < 0) {
			reward = !reward;
		}
		if (reward) {
			strength += amount;
			if (strength > 100) {
				strength = 100;
			}
		}
		else {
			strength -= amount;
			if (strength < -100) {
				strength = -100;
			}
		}

	}
};

std::string computeSynapsePositionHash(const cellPosition& parentNeuron, const cellPosition& childNeuron) {
	std::string holder = "";
	holder += std::to_string(parentNeuron.x);
	holder += std::to_string(parentNeuron.y);
	holder += std::to_string(parentNeuron.z);
	holder += std::to_string(childNeuron.x);
	holder += std::to_string(childNeuron.y);
	holder += std::to_string(childNeuron.z);
	return holder;
}

void createSynapse(const cellPosition& parentNeuron, const std::string& parentHash,
	const cellPosition& childNeuron, const std::string& childHash) {

	std::string newHash = computeSynapsePositionHash(parentNeuron, childNeuron);

	synapse newSynapse;
	newSynapse.hash = newHash; // Assign the computed hash
	newSynapse.parentNeuron = parentNeuron;
	newSynapse.childNeuron = childNeuron;

	synapseMap[newHash] = newSynapse; // Store in map

	//may crash if neurons don't exist

	neuron& p = neuronMap[parentHash];
	p.appendChildHash(newHash);
	neuron& c = neuronMap[childHash];
	c.appendParentHash(newHash);
}

void resetSynapseCharge(const std::string& hash) {
	synapse& syn = synapseMap[hash];
	syn.isCharged = false;
}

void pushSynapseCharge(const std::string& hash) {
	synapse& syn = synapseMap[hash];
	syn.isCharged = true;
}

void updateSynapseStrengths(const bool& reward, const int& amount) {

	for (auto& pair : synapseMap) {
		synapse& syn = pair.second; // Correctly reference the synapse object
		syn.rewardSynapses(reward, amount); // Now functions show up!
	}
}

void calculateReward(const bool& reward) {

	//may mant to instead make a vector of synapses connected to the reward neuron,
	//minimum of two,
	//and only add additional punishment to those in the case of a bad match
	//rather than punishing all as in the current else if
	//not sure tho

	if ((reward && rewardValue > 0) || (!reward && rewardValue < 0)) {
		updateSynapseStrengths(reward, 2);
	}
	else if ((reward && rewardValue <= -1)) {
		updateSynapseStrengths(!reward, 1);
	}
	else {
		updateSynapseStrengths(reward, 1);
	}
}

enum class neuronType { general,reward };

class neuron {
private:
	std::mutex parentHashListMute;
	std::mutex childHashListMute;

public:
	neuronType type;
	bool canConnectParents = true;
	bool canConnectChildren = true;

	//optional general variables
	std::optional<std::vector<std::string>> childSynapseHashes;
	std::optional<bool> canFire;
	std::optional<int> exhaustionLevel;

	//optional reward varibales
	std::optional<int> reverseThreshold;
	std::optional<int> cooldown;

	//initialize optional variables
	neuron(neuronType t) : type(t) {
		if (type == neuronType::reward) {
			reverseThreshold = -75;
			cooldown = 0;
			canConnectChildren = false;
		}
		if (type == neuronType::general) {
			childSynapseHashes = std::vector<std::string>{};
			canFire = true;
			exhaustionLevel = 0;
		}
	}

	//standard variables
	neuronPosition positionData;
	std::vector<std::string> parentSynapseHashes;
	
	float totalInput;
	float neuronCharge = -65;
	int fireThreshold = -55;

	//--------------------------

	void appendParentHash(const std::string& hash) {
		std::lock_guard<std::mutex> lock(parentHashListMute);
		parentSynapseHashes.push_back(hash);
	}
	void appendChildHash(const std::string& hash) {
		if (canConnectChildren) {
			std::lock_guard<std::mutex> lock(childHashListMute);
			childSynapseHashes.value().push_back(hash);
		}
	}
	void removeParentHash(const std::string& hash) {
		std::lock_guard<std::mutex> lock(parentHashListMute);
		parentSynapseHashes.erase(
			std::remove(parentSynapseHashes.begin(), parentSynapseHashes.end(), hash),
			parentSynapseHashes.end()
		);

	}
	void removeChildHash(const std::string& hash) {
		if (canConnectChildren) {
			std::lock_guard<std::mutex> lock(childHashListMute);
			childSynapseHashes.value().erase(
				std::remove(childSynapseHashes.value().begin(), childSynapseHashes.value().end(), hash),
				childSynapseHashes.value().end()
			);
		}
	}

	float calculateInput(const int& strength) {
		//neuron output is 30 in all cases, synapse strngth varies
		return 30 * (strength * 0.1f);
	}

	void tickIn() {

		std::lock_guard<std::mutex> lock(parentHashListMute);

		for (const std::string& synapseHash : parentSynapseHashes) {
			// Retrieve synapse from map
			if (synapseMap.find(synapseHash) != synapseMap.end()) {
				synapse& parentSynapse = synapseMap[synapseHash];

				//skip if synapse has no charge
				if (parentSynapse.isCharged) {
					// Update total input based on synapse properties
					totalInput += calculateInput(parentSynapse.strength);
					resetSynapseCharge(synapseHash);
				}
			}
		}
	}

	void updateChildsynapseCharges() {
		if (canConnectChildren) {
			std::lock_guard<std::mutex> lock(childHashListMute);
			for (const std::string& synapseHash : childSynapseHashes.value()) {
				auto it = synapseMap.find(synapseHash);
				if (it != synapseMap.end()) {
					synapse& childSynapse = it->second;
					pushSynapseCharge(synapseHash);
				}
			}
		}
	}

	void adjustThreshold(int& i) {
		//subject to change
		i = fireThreshold + parentSynapseHashes.size();
	}

	void tickOut() {

		if (type == neuronType::reward) {

			if (cooldown.value() <= 0) {

				if (totalInput > fireThreshold) {
					rewardValue += 1;
					totalInput = 0;
					cooldown.value() = 10;
				}
				else if (totalInput < reverseThreshold.value()) {
					rewardValue -= 1;
					totalInput = 0;
					cooldown.value() = 10;
				}
				else {
					if (totalInput > 2) {
						totalInput -= 2;
					}
					if (totalInput < -2) {
						totalInput += 2;
					}
					else {
						totalInput = 0;
					}
				}
			}
			else {
				cooldown.value() -= 1;
			}
		}

		if (type == neuronType::general) {

			bool shouldFire = false;
			bool fired = false;
			int adjustedThreshold;
			adjustThreshold(adjustedThreshold);

			if (neuronCharge + totalInput > adjustedThreshold) {
				shouldFire = true;
			}

			if (totalInput > 95) {
				totalInput -= 95;
			}
			else {
				totalInput = 0;
			};



			if (shouldFire && canFire.value()) {
				updateChildsynapseCharges();
				pushToFiredNeuronList(positionData);
				fired = true;
			}

			if (fired) {
				if (neuronCharge == -65) {
					neuronCharge = -80;
					exhaustionLevel.value() = 1;
				}
				else {
					neuronCharge = -(80 + exhaustionLevel.value());
					exhaustionLevel.value()++;
				}

				if (neuronCharge < -90) {
					canFire.value() = false;
				}

			}
			else {
				if (neuronCharge < -67) {
					neuronCharge += 2;
				}
				else if (neuronCharge > -63) {
					neuronCharge -= 2;
				}
				else {
					neuronCharge = -65;
				}
				if (neuronCharge == -65) {
					canFire.value() = true;
					exhaustionLevel.value() = 0;
				}

			}
		}

	}

};

std::string computeNeuronPositionHash(const cellPosition& pos) {
	std::string holder = "";
	holder += std::to_string(pos.x);
	holder += std::to_string(pos.y);
	holder += std::to_string(pos.z);
	return holder;
}

int getRandom(const int& low, const int& high) {
	if (high < low) return 1;
	static std::random_device rd;   // Seed source
	static std::mt19937 gen(rd());  // Mersenne Twister RNG
	std::uniform_int_distribution<int> dist(low, high); // Range [low, high]
	return dist(gen);
}

struct gridLayer {
	int scaleLevel = 1;
	int scaleAmount = 3;
	int cubicVolume = 27;
	int layerSize = 26;
};

void scaleUpGridLayer(gridLayer& layer) {

	int prevVolume = layer.cubicVolume;

	layer.scaleLevel++;
	layer.scaleAmount += 2;
	layer.cubicVolume = (layer.scaleAmount * layer.scaleAmount * layer.scaleAmount);
	layer.layerSize = layer.cubicVolume - prevVolume;
}

void mapLayerFace(cellPosition& holder, const gridLayer& layer, 
	const bool& negative, std::vector<cellPosition>& openLayerPositions) {

	int num = 1;
	if (negative) {
		num = -1;
	}

	int remainingPlaces = (layer.scaleAmount * layer.scaleAmount) - 1;
	int countRow = layer.scaleAmount - 1;
	bool reverse = false;
	while (remainingPlaces > 0) {

		if (countRow > 0) {
			if (reverse) {
				holder.y += num;
			}
			else {
				holder.y -= num;
			}
			countRow--;
		}
		else {
			holder.z -= num;
			reverse = !reverse;
			countRow = layer.scaleAmount;
		}
		if (!cellPosOccupied(holder)) {
			openLayerPositions.push_back(holder);
		}
		remainingPlaces--;
	}
}

void mapLayerRemainder(cellPosition& holder, const gridLayer& layer, std::vector<cellPosition>& openLayerPositions) {

	int remainingPlaces = (layer.layerSize - (layer.scaleAmount * layer.scaleAmount) * 2) - 1;
	int cubeSpaces = layer.scaleAmount * 4;
	int countSide = layer.scaleAmount - 1;
	enum class YZ { minusY, minusZ, plusY, plusZ };

	YZ yz = YZ::minusY;

	while (remainingPlaces > 0) {

		switch (yz) {
		case YZ::minusY:
			holder.y -= 1;
			break;
		case YZ::minusZ:
			holder.z -= 1;
			break;
		case YZ::plusY:
			holder.y += 1;
			break;
		case YZ::plusZ:
			holder.z += 1;
			break;
		}

		if (!cellPosOccupied(holder)) {
			openLayerPositions.push_back(holder);
		}

		countSide--;
		remainingPlaces--;

		if (countSide <= 0 && remainingPlaces > 0) {
			int newValue = static_cast<int>(yz) + 1;

			if (newValue < 4) { // Ensure it's within bounds
				yz = static_cast<YZ>(newValue);
				countSide = layer.scaleAmount - 1;
				if (yz == YZ::plusZ) {
					countSide--;
				}
			}
			else {
				yz = YZ::minusY; // Example reset behavior

				//move back to +Y, +Z from +Y, (+Z - 1)
				holder.z += 1;
				// move to (current X) + 1
				holder.x += 1;

				//add current to openLayerPositions if open; 
				if (!cellPosOccupied(holder)) {
					openLayerPositions.push_back(holder);
				}

				remainingPlaces--;
				countSide = layer.scaleAmount - 1;
			}

		}
	}
}

struct layerListSuccess {

	bool success = false;

	std::vector<cellPosition> layerList;

};

layerListSuccess createLayerList(const cellPosition& pos, const gridLayer& layer) {
	
	std::vector<cellPosition> openLayerPositions;


	int toEdge = (layer.scaleAmount - 1) / 2;
	cellPosition holder = pos;
	//shift to outmost +X position
	holder.x += layer.scaleLevel;
	//shift to outmost +Y position
	holder.y += toEdge;
	//shift to outmost +Z position
	holder.z += toEdge;

	//add position +X+Y+Z to openLayerPositions if open; 
	if (!cellPosOccupied(holder)) {
		openLayerPositions.push_back(holder);
	}

	//scan entire +X face
	mapLayerFace(holder, layer, false, openLayerPositions);
	
	//move to -X face
	holder.x -= (layer.scaleLevel * 2);

	//add position -X-Y-Z to openLayerPositions if open; 
	if (!cellPosOccupied(holder)) {
		openLayerPositions.push_back(holder);
	}

	//scan entire -X face
	mapLayerFace(holder, layer, true, openLayerPositions);
	//position should now be -X+Y+Z

	// move to -X face + 1
	holder.x += 1;

	//add position (-X + 1)-Y-Z to openLayerPositions if open; 
	if (!cellPosOccupied(holder)) {
		openLayerPositions.push_back(holder);
	}

	//scan remaindewr of layer
	mapLayerRemainder(holder, layer, openLayerPositions);

	layerListSuccess success;

	if (openLayerPositions.size() > 0) {
		success.success = true;
		success.layerList = openLayerPositions;
	}

	return success;
}


cellPosition findNearbyEmptyPosition(const cellPosition& pos) {

	gridLayer layer;

	layerListSuccess getNewPosition = createLayerList(pos, layer);


	//add some limitation later to prevent from scaling beyond max limit of type long in any direction
	while (!getNewPosition.success) {
		scaleUpGridLayer(layer);
		getNewPosition = createLayerList(pos, layer);
	}

	cellPosition newPos;

	int size = getNewPosition.layerList.size();

	if (size > 1) {
		int randomSelect = getRandom(0, (size - 1));
		newPos = getNewPosition.layerList[randomSelect];
	}
	else {
		newPos = getNewPosition.layerList[0];
	}

	return newPos;

}

void createNeuron(const cellPosition& pos, const neuronType& type) {


	//add some limitation later to prevent from scaling beyond max limit of type long in any direction

	//temporary check for now
	//another function will search for a position based on needs
	//and check if occupied before running this function
	//manual placement will do the same
	if  (cellPosOccupied(pos)) {
		return;
	}

	//only allow one reward neuron
	if (type == neuronType::reward) {
		if (rewardNeuronExists) {
			return;
		}
		else {
			rewardNeuronExists = true;
		}
	}

	std::string newHash = computeNeuronPositionHash(pos);

	neuron newNeuron(type);
	newNeuron.positionData = {pos, newHash};

	neuronMap[newHash] = newNeuron;

	occupiedCellPositions.insert(pos);

}

void placeNearbyNeuron(const cellPosition& pos, const neuronType& type) {

	cellPosition newPos = findNearbyEmptyPosition(pos);
	createNeuron(newPos, type);

}
