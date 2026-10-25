#pragma once
#include <cstdint>
#include <optional>

namespace ph {
struct Recovery {
    std::uint32_t magic{0x50485231}, version{2}, original{}, applied{}, checksum{};
    void seal() { checksum = magic ^ version ^ original ^ applied ^ 0xa532f17cU; }
    bool valid() const {
        return magic == 0x50485231 && original >= 1 && original <= 20 && applied >= 1 && applied <= 20 &&
               ((version == 1 && applied < original) || (version == 2 && applied != original)) &&
               checksum == (magic ^ version ^ original ^ applied ^ 0xa532f17cU);
    }
};
enum class LoadResult { Missing, Found, Error };
struct Backend {
    virtual ~Backend() = default;
    virtual bool get(int& speed) = 0;
    virtual bool set(int speed) = 0;
    virtual bool save(const Recovery& record) = 0;
    virtual bool clear() = 0;
    virtual LoadResult load(Recovery& record) = 0;
};
class WinBackend final : public Backend {
public:
    bool get(int& speed) override;
    bool set(int speed) override;
    bool save(const Recovery& record) override;
    bool clear() override;
    LoadResult load(Recovery& record) override;
};
class SpeedControl {
public:
    explicit SpeedControl(Backend& backend) : backend_(backend) {}
    bool recoverStartup();
    bool activate(int target);
    bool restore();
    bool active() const { return applied_ && record_.has_value(); }
    bool accelerated() const { return active() && record_->applied > record_->original; }
    bool recoverable() const { return record_.has_value(); }
    bool blocked() const { return blocked_; }
private:
    Backend& backend_;
    std::optional<Recovery> record_;
    bool blocked_{};
    bool applied_{};
};
}
