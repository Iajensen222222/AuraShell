using System;
using System.Collections.Generic;
using System.Linq;
using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;

namespace AuraConfig.Pages;

public sealed partial class SchedulePage : Page
{
    private readonly List<ScheduleRule> _rules = new();

    public SchedulePage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // Initialize the singleton with our dispatcher (no-op if already done).
        ScheduleService.Instance.Initialize(DispatcherQueue);

        // Pull current rules from the service into our editable list.
        _rules.Clear();
        foreach (var r in ScheduleService.Instance.Rules) _rules.Add(CloneRule(r));

        SchedulerEnabled.Toggled -= SchedulerEnabled_Toggled;
        SchedulerEnabled.IsOn    = ScheduleService.Instance.Enabled;
        SchedulerEnabled.Toggled += SchedulerEnabled_Toggled;

        RebuildRulesList();
        UpdateNextChange();
    }

    private static ScheduleRule CloneRule(ScheduleRule r) => new()
    {
        Days       = (bool[])r.Days.Clone(),
        StartH     = r.StartH, StartM = r.StartM,
        EndH       = r.EndH,   EndM   = r.EndM,
        ThemeName  = r.ThemeName,
    };

    // ── Rule list rendering ────────────────────────────────────────────────

    private void RebuildRulesList()
    {
        RulesList.Children.Clear();
        EmptyState.Visibility = _rules.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        for (int i = 0; i < _rules.Count; i++)
            RulesList.Children.Add(BuildRuleCard(_rules[i], i));
    }

    private Border BuildRuleCard(ScheduleRule rule, int index)
    {
        string[] dayLabels = { "M", "T", "W", "T", "F", "S", "S" };
        var dayRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        for (int d = 0; d < 7; d++)
        {
            int di = d;
            var btn = new ToggleButton
            {
                Content   = dayLabels[d],
                IsChecked = rule.Days[d],
                Width     = 32, Height = 32,
                Padding   = new Thickness(0),
                FontSize  = 11,
            };
            btn.Checked   += (_, _) => rule.Days[di] = true;
            btn.Unchecked += (_, _) => rule.Days[di] = false;
            dayRow.Children.Add(btn);
        }

        var fromPicker = new TimePicker
        {
            Header          = "From",
            SelectedTime    = new TimeSpan(rule.StartH, rule.StartM, 0),
            ClockIdentifier = "24HourClock",
        };
        fromPicker.SelectedTimeChanged += (_, args) =>
        {
            rule.StartH = args.NewTime?.Hours   ?? rule.StartH;
            rule.StartM = args.NewTime?.Minutes ?? rule.StartM;
        };

        var toPicker = new TimePicker
        {
            Header          = "To",
            SelectedTime    = new TimeSpan(rule.EndH, rule.EndM, 0),
            ClockIdentifier = "24HourClock",
        };
        toPicker.SelectedTimeChanged += (_, args) =>
        {
            rule.EndH = args.NewTime?.Hours   ?? rule.EndH;
            rule.EndM = args.NewTime?.Minutes ?? rule.EndM;
        };

        var themeBox = new ComboBox { Header = "Theme", MinWidth = 160 };
        foreach (var t in ThemeStore.Instance.AllThemes)
            themeBox.Items.Add(t.Name);
        if (!string.IsNullOrEmpty(rule.ThemeName) && themeBox.Items.Contains(rule.ThemeName))
            themeBox.SelectedItem = rule.ThemeName;
        themeBox.SelectionChanged += (_, _) =>
            rule.ThemeName = themeBox.SelectedItem as string ?? "";

        int idx = index;
        var deleteBtn = new Button
        {
            Content           = "Remove",
            VerticalAlignment = VerticalAlignment.Bottom,
        };
        deleteBtn.Click += (_, _) =>
        {
            _rules.RemoveAt(idx);
            RebuildRulesList();
        };

        var timeRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing     = 12,
        };
        timeRow.Children.Add(fromPicker);
        timeRow.Children.Add(toPicker);
        timeRow.Children.Add(themeBox);
        timeRow.Children.Add(deleteBtn);

        var content = new StackPanel { Spacing = 12 };
        content.Children.Add(dayRow);
        content.Children.Add(timeRow);

        return new Border
        {
            Background      = (Brush)Application.Current.Resources["CardBackgroundFillColorDefaultBrush"],
            BorderBrush     = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
            BorderThickness = new Thickness(1),
            CornerRadius    = new CornerRadius(12),
            Padding         = new Thickness(16),
            Child           = content,
        };
    }

    // ── Toolbar handlers ───────────────────────────────────────────────────

    private void AddRule_Click(object sender, RoutedEventArgs e)
    {
        // New rule defaults: weekdays Mon-Fri checked, 9-17, first available theme.
        var rule = new ScheduleRule
        {
            Days   = new[] { true, true, true, true, true, false, false },
            StartH = 9, StartM = 0,
            EndH   = 17, EndM = 0,
            ThemeName = ThemeStore.Instance.AllThemes.FirstOrDefault()?.Name ?? "",
        };
        _rules.Add(rule);
        RebuildRulesList();
    }

    private void SaveRules_Click(object sender, RoutedEventArgs e)
    {
        ScheduleService.Instance.SetRules(_rules.Select(CloneRule).ToList());
        UpdateNextChange();
        ShowStatus("✓  Schedule saved");
    }

    private void SchedulerEnabled_Toggled(object sender, RoutedEventArgs e)
    {
        ScheduleService.Instance.Enabled = SchedulerEnabled.IsOn;
        UpdateNextChange();
    }

    private void UpdateNextChange()
    {
        NextChangeText.Text = SchedulerEnabled.IsOn
            ? ScheduleService.Instance.NextChangeDescription()
            : "Scheduler disabled";
    }

    // ── Status banner ──────────────────────────────────────────────────────

    private async void ShowStatus(string message)
    {
        StatusText.Text         = message;
        StatusBanner.Visibility = Visibility.Visible;
        await Task.Delay(2500);
        StatusBanner.Visibility = Visibility.Collapsed;
    }
}
