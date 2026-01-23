#pragma once

struct DeletionQueue
{
private:
	std::deque<std::function<void()>> _deletors;

public:
	void PushFunction(std::function<void()>&& function) {
		_deletors.push_back(function);
	}

	void Flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto& _deletor : std::ranges::reverse_view(_deletors))
		{
			_deletor(); //call functors
		}

		_deletors.clear();
	}
};
