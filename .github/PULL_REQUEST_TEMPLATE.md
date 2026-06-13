## Summary

<!-- Explain what changed and why. Keep the scope focused. -->

## Affected areas

- [ ] ESP32 firmware
- [ ] Wokwi simulation
- [ ] Raspberry Pi service
- [ ] REST/SSE API or dashboard
- [ ] SQLite storage
- [ ] Deployment scripts
- [ ] Hardware or wiring
- [ ] Documentation

## Verification

<!-- Check every command that was run successfully. Add other checks below. -->

- [ ] `mise run firmware:test`
- [ ] `mise run firmware:compile`
- [ ] `mise run firmware:verify`
- [ ] `mise run simulation:build`
- [ ] `mise run firmware:docs`
- [ ] Manual hardware verification
- [ ] Manual API or dashboard verification

Additional verification:

```text
Command or procedure:
Result:
```

## Behavior and compatibility

<!-- Describe protocol, API, configuration, hardware, storage, or deployment impact. -->

- [ ] No public behavior changed
- [ ] Documentation was updated for behavior or configuration changes
- [ ] Tests were added or updated where practical
- [ ] Backward compatibility was considered

## Evidence

<!-- Add sanitized logs, serial output, API responses, screenshots, or wiring photos when relevant. -->

## Checklist

- [ ] The change is limited to the stated purpose
- [ ] Generated files and build artifacts are not committed
- [ ] No credentials, private network details, or sensitive production data are included
- [ ] Existing unrelated changes were left untouched
