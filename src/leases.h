#pragma once
#include <QHash>
#include <QString>
#include <QUuid>
namespace spectrum {
class Leases {
  public:
    static constexpr qint64 ttlMs = 15000;
    QString acquire(qint64 now) {
        expire(now);
        if (leases.size() >= 64)
            return {};
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        leases.insert(token, now + ttlMs);
        return token;
    }
    bool renew(const QString &token, qint64 now) {
        expire(now);
        if (!leases.contains(token))
            return false;
        leases[token] = now + ttlMs;
        return true;
    }
    void release(const QString &token) { leases.remove(token); }
    void expire(qint64 now) {
        for (auto it = leases.begin(); it != leases.end();)
            if (it.value() <= now)
                it = leases.erase(it);
            else
                ++it;
    }
    int size() const { return int(leases.size()); }
    void clear() { leases.clear(); }

  private:
    QHash<QString, qint64> leases;
};
} // namespace spectrum
