#include "NavigationHistory.h"

void NavigationHistory::visit(const QString &id)
{
    if (current() == id)
        return;
    while (m_entries.size() > m_index + 1)
        m_entries.removeLast();
    m_entries << id;
    m_index = m_entries.size() - 1;
}

bool NavigationHistory::canGoBack() const { return m_index > 0; }
bool NavigationHistory::canGoForward() const { return m_index >= 0 && m_index + 1 < m_entries.size(); }
QString NavigationHistory::back() { if (canGoBack()) --m_index; return current(); }
QString NavigationHistory::forward() { if (canGoForward()) ++m_index; return current(); }
QString NavigationHistory::current() const { return m_index >= 0 ? m_entries.value(m_index) : QString(); }

