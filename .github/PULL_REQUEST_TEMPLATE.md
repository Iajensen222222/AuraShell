## Summary

<!-- 1-3 sentences. What does this PR change and why? -->

## Related issue

<!-- Closes #123, or "N/A" for self-directed work. -->

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor (no behavior change)
- [ ] Performance improvement
- [ ] Documentation
- [ ] Build / CI / tooling

## Coding standard checklist

These are enforced. PRs failing any of these will be asked to revise.

- [ ] All existing unit tests pass (`ctest --output-on-failure`)
- [ ] New tests added for the feature/fix (Catch2, in `tests/unit/`)
- [ ] Every Win32 `HRESULT` is checked with `AURA_HR_CHECK` or `AURA_HR_LOG`
- [ ] Every `HWND` / `HANDLE` / `HKEY` / `HICON` uses an RAII guard
- [ ] No `std::string` for Windows paths — `std::wstring` only
- [ ] No magic numbers — constants are named at module scope or in a constants header
- [ ] No blocking calls on the DirectX render thread
- [ ] Build is clean at `/W4 /WX` (zero warnings)

## Performance verification

If this PR touches the render path, audio capture, or theme application:

- [ ] Idle CPU < 1% measured after change
- [ ] Audio visualizer holds 60 FPS at < 50 ms latency
- [ ] Theme apply time < 500 ms

## Screenshots / recordings

<!-- For UI changes, attach before/after. -->

## Additional notes for reviewer

<!-- Anything reviewers should know, edge cases tested, etc. -->
