using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using AuraConfig.Models;
using Microsoft.UI.Dispatching;

namespace AuraConfig.Services;

/// <summary>
/// Singleton that fires theme-switch events at scheduled wall-clock times.
/// Uses a single one-shot DispatcherQueueTimer that re-arms after each fire,
/// so we never poll. Rules persist to %LOCALAPPDATA%\AuraShell\schedule.json.
/// </summary>
public sealed class ScheduleService
{
    public static ScheduleService Instance { get; } = new();

    public static readonly string FilePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "schedule.json");

    private List<ScheduleRule> _rules = new();
    private DispatcherQueueTimer? _timer;
    private DispatcherQueue?      _dq;
    private bool _enabled = true;

    public IReadOnlyList<ScheduleRule> Rules => _rules;
    public bool Enabled
    {
        get => _enabled;
        set { _enabled = value; if (value) Rearm(); else _timer?.Stop(); }
    }

    private ScheduleService() { }

    /// <summary>Initialize with a dispatcher queue. Call once from any page OnLoaded.</summary>
    public void Initialize(DispatcherQueue dq)
    {
        if (_dq is not null) return; // already initialized
        _dq = dq;
        LoadFromDisk();
        if (_enabled) Rearm();
    }

    public void SetRules(List<ScheduleRule> rules)
    {
        _rules = rules;
        SaveToDisk();
        Rearm();
    }

    // ── Persistence ────────────────────────────────────────────────────────

    private void LoadFromDisk()
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            var list = JsonSerializer.Deserialize<List<ScheduleRule>>(File.ReadAllText(FilePath));
            if (list != null) _rules = list;
        }
        catch { }
    }

    private void SaveToDisk()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllText(FilePath,
                JsonSerializer.Serialize(_rules,
                    new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { }
    }

    // ── Scheduling ─────────────────────────────────────────────────────────

    private void Rearm()
    {
        _timer?.Stop();
        if (_dq is null || _rules.Count == 0 || !_enabled) return;

        var (next, fireAt) = FindNext();
        if (next is null) return;

        var delay = fireAt - DateTime.Now;
        if (delay <= TimeSpan.Zero) delay = TimeSpan.FromSeconds(1);
        if (delay.TotalDays > 7) delay = TimeSpan.FromDays(7); // safety cap

        _timer = _dq.CreateTimer();
        _timer.Interval = delay;
        _timer.IsRepeating = false;
        _timer.Tick += async (_, _) =>
        {
            _timer.Stop();
            await FireRuleAsync(next);
            Rearm();
        };
        _timer.Start();
    }

    /// <summary>Returns the next rule to fire and the absolute DateTime it should fire at.</summary>
    public (ScheduleRule? rule, DateTime when) FindNext()
    {
        if (_rules.Count == 0) return (null, DateTime.MinValue);

        var now  = DateTime.Now;
        DateTime best = DateTime.MaxValue;
        ScheduleRule? bestRule = null;

        foreach (var r in _rules)
        {
            if (string.IsNullOrEmpty(r.ThemeName)) continue;
            // .NET DayOfWeek: Sunday=0..Saturday=6. We store Mon=0..Sun=6.
            // Scan today + next 7 days.
            for (int offset = 0; offset < 8; offset++)
            {
                var day = now.Date.AddDays(offset);
                int idx = ((int)day.DayOfWeek + 6) % 7; // Sun(0)→6, Mon(1)→0...
                if (!r.Days[idx]) continue;
                var fire = day.AddHours(r.StartH).AddMinutes(r.StartM);
                if (fire <= now) continue;
                if (fire < best) { best = fire; bestRule = r; }
                break; // earliest occurrence of this rule is enough
            }
        }
        return (bestRule, best);
    }

    private async System.Threading.Tasks.Task FireRuleAsync(ScheduleRule rule)
    {
        var theme = ThemeStore.Instance.AllThemes.FirstOrDefault(t => t.Name == rule.ThemeName);
        if (theme is null) return;
        await ServiceManager.Instance.PushThemeAsync(theme.ToThemeConfig());
    }

    public string NextChangeDescription()
    {
        var (rule, at) = FindNext();
        if (rule is null) return "No upcoming scheduled changes";
        return $"Next: {rule.ThemeName} at {at:ddd HH:mm}";
    }
}
