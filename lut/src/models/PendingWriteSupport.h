#pragma once

#include <QMap>
#include <QString>
#include <QVariant>
#include <QElapsedTimer>

/**
 * @brief Helper for per-field pending-write tracking
 *
 * Prevents polling from overwriting just-written values until:
 * - The polled value matches the target value (confirmed write)
 * - The timeout expires (allow polling to resume)
 */
class PendingWriteSupport
{
public:
    PendingWriteSupport() = default;

    /**
     * @brief Mark a field as pending write
     * @param fieldKey Unique identifier for the field
     * @param targetValue The value being written
     * @param timeoutMs Timeout in milliseconds (default 2500ms for 1s polling)
     */
    void markPending(const QString &fieldKey, const QVariant &targetValue, int timeoutMs = 2500);

    /**
     * @brief Clear pending state for a field
     * @param fieldKey Field identifier
     */
    void clearPending(const QString &fieldKey);

    /**
     * @brief Check if a field has an active pending write
     * @param fieldKey Field identifier
     * @return true if pending and not expired
     */
    bool hasPending(const QString &fieldKey) const;

    /**
     * @brief Determine if a polled value should be applied to the model
     * @param fieldKey Field identifier
     * @param polledValue Value read from device
     * @return true if should apply (no pending or expired), false if should skip
     *
     * Side effect: If polled value matches pending target, clears pending
     */
    bool shouldAcceptPolledValue(const QString &fieldKey, const QVariant &polledValue);

private:
    struct PendingState {
        QVariant targetValue;
        QElapsedTimer timer;
        int timeoutMs;
    };

    QMap<QString, PendingState> m_pending;
};
