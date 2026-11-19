#pragma warning(push)
#pragma warning(disable:4100)
HRESULT STDMETHODCALLTYPE SetFocus( void) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetRuntimeId( 
              SAFEARRAY * *runtimeId) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE FindAll( 
             enum TreeScope scope,
              IUIAutomationCondition *condition,
              IUIAutomationElementArray **found) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE FindFirstBuildCache( 
             enum TreeScope scope,
              IUIAutomationCondition *condition,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **found) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE FindAllBuildCache( 
             enum TreeScope scope,
              IUIAutomationCondition *condition,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElementArray **found) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE BuildUpdatedCache( 
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **updatedElement) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCurrentPropertyValue( 
             PROPERTYID propertyId,
              VARIANT *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCurrentPropertyValueEx( 
             PROPERTYID propertyId,
             BOOL ignoreDefaultValue,
              VARIANT *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCachedPropertyValue( 
             PROPERTYID propertyId,
              VARIANT *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCachedPropertyValueEx( 
             PROPERTYID propertyId,
             BOOL ignoreDefaultValue,
              VARIANT *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCurrentPatternAs( 
             PATTERNID patternId,
              REFIID riid,
              void **patternObject) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCachedPatternAs( 
             PATTERNID patternId,
              REFIID riid,
              void **patternObject) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCurrentPattern( 
             PATTERNID patternId,
              IUnknown **patternObject) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCachedPattern( 
             PATTERNID patternId,
              IUnknown **patternObject) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCachedParent( 
              IUIAutomationElement **parent) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetCachedChildren( 
              IUIAutomationElementArray **children) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentProcessId( 
              int *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentControlType( 
              CONTROLTYPEID *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentLocalizedControlType( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentAcceleratorKey( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentAccessKey( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentHasKeyboardFocus( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsKeyboardFocusable( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsEnabled( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentAutomationId( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentClassName( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentHelpText( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentCulture( 
              int *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsControlElement( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsContentElement( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsPassword( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentNativeWindowHandle( 
              UIA_HWND *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentItemType( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsOffscreen( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentOrientation( 
              enum OrientationType *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentFrameworkId( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsRequiredForForm( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentItemStatus( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentBoundingRectangle( 
              RECT *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentLabeledBy( 
              IUIAutomationElement **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentAriaRole( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentAriaProperties( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentIsDataValidForForm( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentControllerFor( 
              IUIAutomationElementArray **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentDescribedBy( 
              IUIAutomationElementArray **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentFlowsTo( 
              IUIAutomationElementArray **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CurrentProviderDescription( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedProcessId( 
              int *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedControlType( 
              CONTROLTYPEID *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedLocalizedControlType( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedName( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedAcceleratorKey( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedAccessKey( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedHasKeyboardFocus( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsKeyboardFocusable( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsEnabled( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedAutomationId( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedClassName( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedHelpText( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedCulture( 
              int *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsControlElement( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsContentElement( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsPassword( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedNativeWindowHandle( 
              UIA_HWND *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedItemType( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsOffscreen( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedOrientation( 
              enum OrientationType *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedFrameworkId( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsRequiredForForm( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedItemStatus( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedBoundingRectangle( 
              RECT *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedLabeledBy( 
              IUIAutomationElement **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedAriaRole( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedAriaProperties( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedIsDataValidForForm( 
              BOOL *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedControllerFor( 
              IUIAutomationElementArray **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedDescribedBy( 
              IUIAutomationElementArray **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedFlowsTo( 
              IUIAutomationElementArray **retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_CachedProviderDescription( 
              BSTR *retVal) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetClickablePoint( 
              POINT *clickable,
              BOOL *gotClickable) override {return E_NOTIMPL;}
#pragma warning(pop)
