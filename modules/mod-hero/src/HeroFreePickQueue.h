#ifndef EVRY_HERO_FREE_PICK_QUEUE_H
#define EVRY_HERO_FREE_PICK_QUEUE_H

#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HeroFreePick
{
// Producers may run on different map workers. Pump and all operations run on
// the world thread, after the map workers have joined. An operation owns its
// character's lane until its durable completion/readback invokes Done.
template<class Key>
class OrderedOperations
{
public:
    using Done = std::function<void()>;
    using Operation = std::function<void(Done)>;

    void Enqueue(Key key, Operation operation)
    {
        std::lock_guard lock(_mutex);
        _lanes[key].Waiting.push_back(std::move(operation));
    }

    void Pump()
    {
        std::vector<std::pair<Key, Operation>> ready;
        {
            std::lock_guard lock(_mutex);
            for (auto& [key, lane] : _lanes)
                if (!lane.Active && !lane.Waiting.empty())
                {
                    lane.Active = true;
                    ready.emplace_back(key, std::move(lane.Waiting.front()));
                    lane.Waiting.pop_front();
                }
        }
        for (auto& [key, operation] : ready)
            operation([this, key]
            {
                std::lock_guard lock(_mutex);
                auto it = _lanes.find(key);
                if (it == _lanes.end())
                    return;
                it->second.Active = false;
                if (it->second.Waiting.empty())
                    _lanes.erase(it);
            });
    }

private:
    struct Lane
    {
        bool Active = false;
        std::deque<Operation> Waiting;
    };
    std::mutex _mutex;
    std::unordered_map<Key, Lane> _lanes;
};
}
#endif
