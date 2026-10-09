// Steam's gamepad DropDownMenu renderer ignores desktop positioning and opens a
// centered modal. Keep its native trigger, but expand native Focusable options
// in the same QAM row. No popup, portal, global patch or second focus tree.
import type { Choice } from './mirrorClient';
declare const SP_REACT: any;
declare const DFL: any;
const React = SP_REACT;
export function AnchoredDropdown(props: { label: any; selectedOption: string; rgOptions: Choice[]; disabled: boolean;
  opened: boolean; onMenuWillOpen: (show: () => void) => boolean; onCancel: () => void; onChange: (choice: Choice) => void }) {
  const root = React.useRef(null), trigger = React.useRef(null), wasOpen = React.useRef(false);
  const cancel = (event?: any) => { event?.stopPropagation?.(); event?.preventDefault?.(); props.onCancel(); return true; };
  React.useEffect(() => {
    if (props.opened) {
      const selected = root.current?.querySelector('[role="option"][aria-selected="true"]:not([aria-disabled="true"])');
      (selected || root.current?.querySelector('[role="option"]:not([aria-disabled="true"])'))?.focus();
    } else if (wasOpen.current && !props.disabled) trigger.current?.element?.focus();
    wasOpen.current = props.opened;
  }, [props.opened]);
  const keys = (event: any) => {
    if (!props.opened) return;
    if (event.key === 'Escape') { cancel(event); return; }
    if (!['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
    const options = Array.from(root.current?.querySelectorAll('[role="option"]:not([aria-disabled="true"])') || []) as HTMLElement[];
    const current = options.indexOf(root.current?.ownerDocument?.activeElement);
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? options.length - 1 : (current + (event.key === 'ArrowUp' ? -1 : 1) + options.length) % options.length;
    options[next]?.focus(); event.preventDefault(); event.stopPropagation();
  };
  return <div ref={root} className="ymcc-anchored-dropdown" onKeyDownCapture={keys}>
    <DFL.DropdownItem label={props.label} selectedOption={props.selectedOption} rgOptions={props.rgOptions}
      controlled={true} childrenContainerWidth="max" bottomSeparator="none" disabled={props.disabled}
      dropDownControlRef={trigger} onMenuWillOpen={() => { if (props.opened) props.onCancel(); else props.onMenuWillOpen(() => {}); return false; }}
      onChange={(option: Choice) => { if (!props.opened && !props.disabled && !option.disabled) { props.onMenuWillOpen(() => {}); props.onChange(option); } }} />
    {props.opened && <DFL.Focusable className="ymcc-anchored-options" role="listbox" aria-label="方案选项"
      flow-children="column" focusable={false} onCancel={cancel}>
      {props.rgOptions.map(option => <DFL.Focusable key={option.data} role="option" aria-selected={option.data === props.selectedOption}
        aria-disabled={!!option.disabled} focusable={!option.disabled} preferredFocus={option.data === props.selectedOption && !option.disabled}
        className="ymcc-anchored-option" onActivate={() => { if (!option.disabled) props.onChange(option); }}>
        {option.label}
      </DFL.Focusable>)}
      <DFL.Focusable role="button" className="ymcc-anchored-option ymcc-anchored-cancel" focusable={true} onActivate={cancel}>取消</DFL.Focusable>
    </DFL.Focusable>}
  </div>;
}
