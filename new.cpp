#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <set>

bool clockState = false;

void tick() {
	clockState = !clockState;
}


std::shared_mutex neuronMapMutex;
std::unordered_map<std::string, std::unique_ptr<Neuron>> neuronMap;

enum class NeuronType { generic, reward, input, output };


struct cellPosition {
	long x, y, z;
};

struct neuronPosition {
	cellPosition Position;
	std::string hash;
};

void pushToNeuron(std::string hash, int strength) {
	std::shared_lock<std::shared_mutex> lock(neuronMapMutex);

	auto it = neuronMap.find(hash);  // Check existence AFTER locking
	if (it == neuronMap.end()) {
		return;  // Prevents access to a deleted entry
	}
	Neuron& c = *(it->second);
	if (auto* neuron = dynamic_cast<NeuronWithParents*>(&c)) {
		neuron->wakeNeuron(strength);
	}
}

struct Synapse {

	std::string parentNeuronHash;
	std::string childNeuronHash;
	//manually set neuron strength to 100 when setting up bas netowrk synapses
	//cap total value at maybe 1000
	int strength = 1;

	std::mutex chargeMute;
	void chargeSynapse() {
		std::lock_guard<std::mutex> lock(chargeMute);
		std::thread t(pushToNeuron, childNeuronHash, strength);
		t.detach();
	}

	int age = 0;

	void rewardSynapse(bool reward, const int& amount) {
		std::lock_guard<std::mutex> lock(chargeMute);
		
		int ageMultiplyer = 0;

		//young synapses should respond more strongly to feedback
		//older neurons will benefit from more stability

		if (age < 5) {
			ageMultiplyer = 20;
		}
		else if (age < 20) {
			ageMultiplyer = 5;
		}
		else if (age < 100) {
			ageMultiplyer = 2;
		}

		if (strength < 0) {
			reward = !reward;
		}
		if (reward) {
			strength += (amount * ageMultiplyer);
			if (strength > 1000) {
				strength = 1000;
			}
		}
		else {
			strength -= (amount * ageMultiplyer);
			if (strength < -1000) {
				strength = -1000;
			}
		}
		age++;
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

std::shared_mutex synapseMapMutex;
std::unordered_map<std::string, std::unique_ptr<Synapse>> synapseMap;

void createSynapse(const cellPosition& parentNeuron, const std::string& parentHash,
	const cellPosition& childNeuron, const std::string& childHash) {

	std::string newHash = computeSynapsePositionHash(parentNeuron, childNeuron);

	std::unique_ptr<Synapse> newSynapse = std::make_unique<Synapse>();

	newSynapse->parentNeuronHash = parentHash;
	newSynapse->childNeuronHash = childHash;

	{
		std::unique_lock<std::shared_mutex> lock(synapseMapMutex);
		synapseMap[newHash] = std::move(newSynapse);
	}

	std::shared_lock<std::shared_mutex> lock(neuronMapMutex);
	Neuron& p = *neuronMap[parentHash];
	if (auto* ThisParentNeuron = dynamic_cast<NeuronWithChildren*>(&p)) {
		ThisParentNeuron->appendChildHash(newHash);
	}
	Neuron& c = *neuronMap[childHash];
	if (auto* ThisChildNeuron = dynamic_cast<NeuronWithParents*>(&c)) {
		ThisChildNeuron->appendParentHash(newHash);
	}
}

std::mutex occupiedPositionsMute;
std::set<cellPosition> occupiedCellPositions;

bool cellPosOccupied(const cellPosition& pos) {
	return occupiedCellPositions.find(pos) != occupiedCellPositions.end();
}

std::string computeNeuronPositionHash(const cellPosition& pos) {
	std::string holder = "";
	holder += std::to_string(pos.x);
	holder += std::to_string(pos.y);
	holder += std::to_string(pos.z);
	return holder;
}

void createNeuron(cellPosition pos, NeuronType type) {

	static bool rewardNeuronExists = false;

	if (type == NeuronType::reward) {
		if (rewardNeuronExists) {
			return;
		}
		else {
			rewardNeuronExists = true;
		}
	}

	if (type == NeuronType::generic) {

		std::lock_guard<std::mutex> lock(occupiedPositionsMute);
		if (cellPosOccupied(pos)) {
			return;
		}
		occupiedCellPositions.insert(pos);
		
		std::string newHash = computeNeuronPositionHash(pos);
		std::unique_ptr<Neuron> newNeuron = std::make_unique<GenericNeuron>();

		newNeuron->positionData = { pos, newHash };

		std::unique_lock<std::shared_mutex> lock(neuronMapMutex);
		neuronMap.try_emplace(newHash, std::move(newNeuron));

	}
}


float calculateInput(const int& strength) {
	//neuron output is 30 in all cases, synapse strngth varies
	return 30 * (strength * 0.1f);
}

class Neuron {
public:
	virtual ~Neuron() = default;
	neuronPosition positionData;

	virtual void fire() = 0;

	

};

class NeuronWithParents : virtual public Neuron {
public:

	int input = 0;
	float neuronCharge = -65;
	int fireThreshold = -55;
	
	std::mutex wakeMute;
	virtual void wakeNeuron(const int& strength) {
		std::lock_guard<std::mutex> lock(wakeMute);
		input += calculateInput(strength);
		if (input > fireThreshold) {
			fire();
		}
	}
	
	std::vector<std::string> parentSynapseHashes;
	std::mutex parentHashListMute;
	
	void appendParentHash(const std::string& hash) {
		std::lock_guard<std::mutex> lock(parentHashListMute);
		parentSynapseHashes.push_back(hash);
	}

	void removeParentHash(const std::string& hash) {
		std::lock_guard<std::mutex> lock(parentHashListMute);
		parentSynapseHashes.erase(
			std::remove(parentSynapseHashes.begin(), parentSynapseHashes.end(), hash),
			parentSynapseHashes.end()
		);
	}

};

void pushSynapseCharge(std::string hash) {
	std::shared_lock<std::shared_mutex> lock(synapseMapMutex);

	auto it = synapseMap.find(hash);
	if (it == synapseMap.end()) {
		return;
	}

	Synapse& s = *(it->second);
	s.chargeSynapse();
}

class NeuronWithChildren : virtual public Neuron {
public:
	std::mutex childHashListMute;
	std::vector<std::string> childSynapseHashes;

	void appendChildHash(const std::string& hash) {
			std::lock_guard<std::mutex> lock(childHashListMute);
			childSynapseHashes.push_back(hash);
	}

	void removeChildHash(const std::string& hash) {
			std::lock_guard<std::mutex> lock(childHashListMute);
			childSynapseHashes.erase(
				std::remove(childSynapseHashes.begin(), childSynapseHashes.end(), hash),
				childSynapseHashes.end()
			);
	}

	void chargeChildSynapses() {

		std::vector<std::string> out;
		{
			std::lock_guard<std::mutex> lock(childHashListMute);
			out = childSynapseHashes;
		}

		for (const std::string& synapseHash : out) {
			std::thread t(pushSynapseCharge, synapseHash);
			t.detach();
		}
	}

};

void adjustThreshold(int& i, const int& fireThreshold, const std::vector<std::string>& parentHashes) {
	//subject to change
	i = fireThreshold + parentHashes.size();
}

class GenericNeuron : public NeuronWithParents, public NeuronWithChildren {

	std::mutex firingMute;
	bool counting = true;

	void countRecovery(bool& prevState, int& neuroCharge, bool& caFire,
	int& exhaustLevel, int& in) {

		bool run = true;
		while (run) {

			if (prevState != clockState) {

				std::lock_guard<std::mutex> lock(firingMute);

				prevState = clockState;

				if (in > 0) {
					if (in >= 2) {
						in -= 2;
					}
					else if (in < 2) {
						in = 0;
					}
				}

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
					caFire = true;
					exhaustLevel = 0;
					run = false;
					counting = false;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	bool previousclockState = false;

	bool canFire = true;
	int exhaustionLevel = 0;

	void fire() {

		if (canFire) {
			std::lock_guard<std::mutex> lock(firingMute);
			if (input > 95) {
				input -= 95;
			}
			else {
				input = 0;
			};
			chargeChildSynapses();
			
			bool shouldCount = false;

			if (neuronCharge == -65) {
				neuronCharge = -80;
				exhaustionLevel = 1;
			}
			else {
				neuronCharge = -(80 + exhaustionLevel);
				exhaustionLevel++;
				if (!counting) {
					shouldCount = true;
				}
			}
			if (neuronCharge < -90) {
				canFire = false;
			}
			if (shouldCount) {
				counting = true;
				std::thread t(&GenericNeuron::countRecovery, this, 
					std::ref(previousclockState), std::ref(neuronCharge), 
					std::ref(canFire), std::ref(exhaustionLevel), std::ref(input));
				t.detach();
			}
		}
	}

	void wakeNeuron(const int& strength) {
		std::lock_guard<std::mutex> lock(wakeMute);
		input += calculateInput(strength);
		int adjustedThreshold;
		std::vector<std::string> in;
		{
			std::lock_guard<std::mutex> lock(parentHashListMute);
			in = parentSynapseHashes;
		}
		adjustThreshold(adjustedThreshold, fireThreshold, in);
		if (adjustedThreshold > fireThreshold) {
			fire();
		}
	}

};