/** Static image imports are resolved to asset URLs by Vite. */
declare module '*.png' {
  const url: string;
  export default url;
}
