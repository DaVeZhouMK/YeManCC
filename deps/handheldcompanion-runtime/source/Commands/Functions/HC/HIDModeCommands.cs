using HandheldCompanion.Managers;
using HandheldCompanion.Utils;
using System;

namespace HandheldCompanion.Commands.Functions.HC
{
    [Serializable]
    public class HIDModeCommands : FunctionCommands
    {
        private const string SettingsName = "HIDmode";

        public HIDModeCommands()
        {
            base.Name = Properties.Resources.Hotkey_ChangeHIDMode;
            base.Description = Properties.Resources.Hotkey_ChangeHIDModeDesc;
            base.OnKeyUp = true;
            base.FontFamily = "PromptFont";
            base.Glyph = "\u243C";

            Update();

            ManagerFactory.profileManager.Applied += ProfileManager_Applied;
        }

        private void ProfileManager_Applied(Profile profile, UpdateSource source)
        {
            IsEnabled = profile.HID == HIDmode.NotSelected;
            Update(profile.HID);
        }

        public void Update(HIDmode profileMode = HIDmode.NotSelected)
        {
            HIDmode currentHIDmode = profileMode == HIDmode.NotSelected ? (HIDmode)ManagerFactory.settingsManager.GetInt(SettingsName, true) : profileMode;
            switch (currentHIDmode)
            {
                case HIDmode.Xbox360Controller:
                    LiveGlyph = "\uE001";
                    break;
                case HIDmode.DualShock4Controller:
                case HIDmode.DualSenseController:
                    LiveGlyph = "\uE000";
                    break;
                case HIDmode.SteamDeckController:
                case HIDmode.SteamController:
                case HIDmode.SwitchProController:
                    LiveGlyph = "\u243C";
                    break;
            }

            base.Update();
        }

        public override void Execute(bool IsKeyDown, bool IsKeyUp, bool IsBackground)
        {
            if (IsEnabled)
            {
                HIDmode currentHIDmode = (HIDmode)ManagerFactory.settingsManager.GetInt(SettingsName, true);
                switch (currentHIDmode)
                {
                    case HIDmode.Xbox360Controller:
                        ManagerFactory.settingsManager.SetProperty(SettingsName, (int)HIDmode.DualShock4Controller);
                        break;
                    case HIDmode.DualShock4Controller:
                        ManagerFactory.settingsManager.SetProperty(SettingsName, (int)HIDmode.DualSenseController);
                        break;
                    case HIDmode.DualSenseController:
                        ManagerFactory.settingsManager.SetProperty(SettingsName, (int)HIDmode.SteamDeckController);
                        break;
                    case HIDmode.SteamDeckController:
                        ManagerFactory.settingsManager.SetProperty(SettingsName, (int)HIDmode.SteamController);
                        break;
                    case HIDmode.SteamController:
                        ManagerFactory.settingsManager.SetProperty(SettingsName, (int)HIDmode.SwitchProController);
                        break;
                    case HIDmode.SwitchProController:
                        ManagerFactory.settingsManager.SetProperty(SettingsName, (int)HIDmode.Xbox360Controller);
                        break;
                    default:
                        break;
                }
            }

            Update();
            base.Execute(IsKeyDown, IsKeyUp, false);
        }

        public override object Clone()
        {
            HIDModeCommands commands = new()
            {
                commandType = this.commandType,
                Name = this.Name,
                Description = this.Description,
                FontFamily = this.FontFamily,
                Glyph = this.Glyph,
                LiveGlyph = this.LiveGlyph,
                OnKeyUp = this.OnKeyUp,
                OnKeyDown = this.OnKeyDown,
            };

            return commands;
        }

        public override void Dispose()
        {
            ManagerFactory.profileManager.Applied -= ProfileManager_Applied;
            base.Dispose();
        }
    }
}
