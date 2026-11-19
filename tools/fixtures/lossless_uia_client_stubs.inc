#pragma warning(push)
#pragma warning(disable:4100)
HRESULT STDMETHODCALLTYPE CompareElements( 
              IUIAutomationElement *el1,
              IUIAutomationElement *el2,
              BOOL *areSame) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CompareRuntimeIds( 
              SAFEARRAY * runtimeId1,
              SAFEARRAY * runtimeId2,
              BOOL *areSame) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetRootElement( 
              IUIAutomationElement **root) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE ElementFromPoint( 
             POINT pt,
              IUIAutomationElement **element) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetFocusedElement( 
              IUIAutomationElement **element) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetRootElementBuildCache( 
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **root) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE ElementFromHandleBuildCache( 
              UIA_HWND hwnd,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **element) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE ElementFromPointBuildCache( 
             POINT pt,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **element) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetFocusedElementBuildCache( 
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **element) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateTreeWalker( 
              IUIAutomationCondition *pCondition,
              IUIAutomationTreeWalker **walker) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ControlViewWalker( 
              IUIAutomationTreeWalker **walker) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ContentViewWalker( 
              IUIAutomationTreeWalker **walker) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_RawViewWalker( 
              IUIAutomationTreeWalker **walker) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_RawViewCondition( 
              IUIAutomationCondition **condition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ControlViewCondition( 
              IUIAutomationCondition **condition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ContentViewCondition( 
              IUIAutomationCondition **condition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateCacheRequest( 
              IUIAutomationCacheRequest **cacheRequest) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateTrueCondition( 
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateFalseCondition( 
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreatePropertyConditionEx( 
             PROPERTYID propertyId,
             VARIANT value,
             enum PropertyConditionFlags flags,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateAndCondition( 
              IUIAutomationCondition *condition1,
              IUIAutomationCondition *condition2,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateAndConditionFromArray( 
              SAFEARRAY * conditions,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateAndConditionFromNativeArray( 
              IUIAutomationCondition **conditions,
             int conditionCount,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateOrCondition( 
              IUIAutomationCondition *condition1,
              IUIAutomationCondition *condition2,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateOrConditionFromArray( 
              SAFEARRAY * conditions,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateOrConditionFromNativeArray( 
              IUIAutomationCondition **conditions,
             int conditionCount,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateNotCondition( 
              IUIAutomationCondition *condition,
              IUIAutomationCondition **newCondition) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE AddAutomationEventHandler( 
             EVENTID eventId,
              IUIAutomationElement *element,
             enum TreeScope scope,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationEventHandler *handler) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE RemoveAutomationEventHandler( 
             EVENTID eventId,
              IUIAutomationElement *element,
              IUIAutomationEventHandler *handler) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE AddPropertyChangedEventHandler( 
              IUIAutomationElement *element,
             enum TreeScope scope,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationPropertyChangedEventHandler *handler,
              SAFEARRAY * propertyArray) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE AddStructureChangedEventHandler( 
              IUIAutomationElement *element,
             enum TreeScope scope,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationStructureChangedEventHandler *handler) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE RemoveStructureChangedEventHandler( 
              IUIAutomationElement *element,
              IUIAutomationStructureChangedEventHandler *handler) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE AddFocusChangedEventHandler( 
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationFocusChangedEventHandler *handler) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE RemoveFocusChangedEventHandler( 
              IUIAutomationFocusChangedEventHandler *handler) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE RemoveAllEventHandlers( void) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IntNativeArrayToSafeArray( 
              int *array,
             int arrayCount,
              SAFEARRAY * *safeArray) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE IntSafeArrayToNativeArray( 
              SAFEARRAY * intArray,
              int **array,
              int *arrayCount) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE RectToVariant( 
             RECT rc,
              VARIANT *var) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE VariantToRect( 
             VARIANT var,
              RECT *rc) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE SafeArrayToRectNativeArray( 
              SAFEARRAY * rects,
              RECT **rectArray,
              int *rectArrayCount) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CreateProxyFactoryEntry( 
              IUIAutomationProxyFactory *factory,
              IUIAutomationProxyFactoryEntry **factoryEntry) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ProxyFactoryMapping( 
              IUIAutomationProxyFactoryMapping **factoryMapping) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetPropertyProgrammaticName( 
             PROPERTYID property,
              BSTR *name) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE GetPatternProgrammaticName( 
             PATTERNID pattern,
              BSTR *name) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE PollForPotentialSupportedPatterns( 
              IUIAutomationElement *pElement,
              SAFEARRAY * *patternIds,
              SAFEARRAY * *patternNames) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE PollForPotentialSupportedProperties( 
              IUIAutomationElement *pElement,
              SAFEARRAY * *propertyIds,
              SAFEARRAY * *propertyNames) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE CheckNotSupported( 
             VARIANT value,
              BOOL *isNotSupported) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ReservedNotSupportedValue( 
              IUnknown **notSupportedValue) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE get_ReservedMixedAttributeValue( 
              IUnknown **mixedAttributeValue) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE ElementFromIAccessible( 
              IAccessible *accessible,
             int childId,
              IUIAutomationElement **element) override {return E_NOTIMPL;}
HRESULT STDMETHODCALLTYPE ElementFromIAccessibleBuildCache( 
              IAccessible *accessible,
             int childId,
              IUIAutomationCacheRequest *cacheRequest,
              IUIAutomationElement **element) override {return E_NOTIMPL;}

#pragma warning(pop)
