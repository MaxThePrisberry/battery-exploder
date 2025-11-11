# Battery Exploder Documentation

This directory contains centralized documentation for the Battery Exploder project.

## Directory Structure

### `/eclab/` - EC-Lab OLE COM Documentation
EC-Lab integration, troubleshooting, and implementation guides:
- `ECLAB_INTEGRATION_GUIDE.md` - Main integration guide for EC-Lab mode
- `ECLAB_OLECOM_Implementation_Plan.md` - Original implementation plan
- `ECLAB_OLECOM_Debugging_Checklist.md` - Debugging checklist
- `ECLAB_COM_TROUBLESHOOTING.md` - Common issues and solutions
- `ECLAB_CUSTOM_INTERFACE_IMPLEMENTATION.md` - Custom interface details
- `ECLabCOM_EClabExeInterface.txt` - Interface specifications

### `/integration/` - Feature Integration Guides
Guides for integrating various hardware and software components:
- `CLI_and_scripting_development_plan.md` - CLI and scripting features
- `Pressure_Safety_Integration_Guide.md` - Pressure safety monitoring
- `4-20mA_Integration_Summary.txt` - 4-20mA current sensors (NI 9202)
- `DTB_RAMP_SOAK_UI_INTEGRATION.txt` - DTB temperature controller ramp-soak
- `ALICAT_TEST_UI_INTEGRATION.txt` - ALICAT mass flow controller integration
- `UI_INSTRUCTIONS_AUTO_TUNE_CHECKBOX.txt` - DTB auto-tuning UI
- `exp_temp_ramp_implementation_plan.txt` - Temperature ramp EIS experiment

### `/troubleshooting/` - Debugging and Problem Solving
Known issues, workarounds, and technical debt:
- `EC-Lab_Relay_Switch_Issue_Summary.txt` - Detailed analysis of relay switching issue (RESOLVED)
- `DAQ_Commands_Reference.txt` - DAQmx command reference
- `TECHNICAL_DEBT.txt` - Known technical debt and future improvements

### `/testing/` - Test Plans and Results
Testing documentation and results:
- `eclab_olecom_test_plan.md` - EC-Lab COM testing procedures
- `TEST_RESULTS.md` - Test execution results
- `cdaq_current_test_README.md` - cDAQ current sensor testing

### `/logs/` - Experiment and Debug Logs
Historical log files from testing and debugging:
- `log 2025-11-*.txt` - Recent experiment logs
- `log 2025-11-06-*.txt` - Device configuration testing logs
- `build_report_*.txt` - Build diagnostics
- `alicat logs.txt` - ALICAT device testing

## Related Documentation

- `/CLAUDE.md` - Main project documentation for Claude Code (root directory)
- `/README.md` - Project overview and setup (root directory)
- `/python_testing/` - Python testing tools and guides (separate directory)

## Key Resolved Issues

### EC-Lab Relay Switching Issue (November 2025)
**Problem:** OCV/GEIS measurements failed after relay switching with RPC_E_DISCONNECTED error.

**Root Cause:** COM threading issue - experiment thread wasn't initializing COM before calling EC-Lab methods.

**Solution:** Added `CoInitializeEx(COINIT_MULTITHREADED)` to experiment thread initialization.

**Commits:**
- `0540d11` - Implemented force reconnect with polling mechanism
- `d470b72` - Fixed COM threading initialization

See `troubleshooting/EC-Lab_Relay_Switch_Issue_Summary.txt` for detailed analysis.

## Navigation Tips

1. **Setting up EC-Lab?** Start with `/eclab/ECLAB_INTEGRATION_GUIDE.md`
2. **EC-Lab not working?** Check `/eclab/ECLAB_COM_TROUBLESHOOTING.md`
3. **Adding new feature?** Look for similar guides in `/integration/`
4. **Debugging issue?** Check `/troubleshooting/` for known problems
5. **Running tests?** See `/testing/` for test procedures

## Document Maintenance

When adding new documentation:
- Place in appropriate subdirectory based on topic
- Update this README with link in relevant section
- Use descriptive filenames with dates if time-sensitive
- Keep CLAUDE.md synchronized with major changes

---

**Last Updated:** 2025-11-11
**Organization:** Centralized from scattered notes/ and biologic/ locations
