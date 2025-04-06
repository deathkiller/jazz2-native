<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>
<tagfile doxygen_version="1.12.0">
  <compound kind="file">
    <name>angelscript.h</name>
    <path>source/</path>
    <filename>angelscript_8h.html</filename>
  </compound>
  <compound kind="class">
    <name>asIBinaryStream</name>
    <filename>classas_i_binary_stream.html</filename>
  </compound>
  <compound kind="class">
    <name>asIJITCompiler</name>
    <filename>classas_i_j_i_t_compiler.html</filename>
    <base>asIJITCompilerAbstract</base>
  </compound>
  <compound kind="class">
    <name>asIJITCompilerAbstract</name>
    <filename>classas_i_j_i_t_compiler_abstract.html</filename>
  </compound>
  <compound kind="class">
    <name>asIJITCompilerV2</name>
    <filename>classas_i_j_i_t_compiler_v2.html</filename>
    <base>asIJITCompilerAbstract</base>
  </compound>
  <compound kind="class">
    <name>asILockableSharedBool</name>
    <filename>classas_i_lockable_shared_bool.html</filename>
  </compound>
  <compound kind="class">
    <name>asIScriptContext</name>
    <filename>classas_i_script_context.html</filename>
  </compound>
  <compound kind="class">
    <name>asIScriptEngine</name>
    <filename>classas_i_script_engine.html</filename>
  </compound>
  <compound kind="class">
    <name>asIScriptFunction</name>
    <filename>classas_i_script_function.html</filename>
  </compound>
  <compound kind="class">
    <name>asIScriptGeneric</name>
    <filename>classas_i_script_generic.html</filename>
  </compound>
  <compound kind="class">
    <name>asIScriptModule</name>
    <filename>classas_i_script_module.html</filename>
  </compound>
  <compound kind="class">
    <name>asIScriptObject</name>
    <filename>classas_i_script_object.html</filename>
  </compound>
  <compound kind="class">
    <name>asIStringFactory</name>
    <filename>classas_i_string_factory.html</filename>
  </compound>
  <compound kind="class">
    <name>asIThreadManager</name>
    <filename>classas_i_thread_manager.html</filename>
  </compound>
  <compound kind="class">
    <name>asITypeInfo</name>
    <filename>classas_i_type_info.html</filename>
  </compound>
  <compound kind="struct">
    <name>asSBCInfo</name>
    <filename>structas_s_b_c_info.html</filename>
  </compound>
  <compound kind="struct">
    <name>asSFuncPtr</name>
    <filename>structas_s_func_ptr.html</filename>
  </compound>
  <compound kind="struct">
    <name>asSMessageInfo</name>
    <filename>structas_s_message_info.html</filename>
  </compound>
  <compound kind="struct">
    <name>asSVMRegisters</name>
    <filename>structas_s_v_m_registers.html</filename>
  </compound>
  <compound kind="group">
    <name>api_principal_interfaces</name>
    <title>Principal interfaces</title>
    <filename>group__api__principal__interfaces.html</filename>
    <class kind="class">asIScriptEngine</class>
    <class kind="class">asIScriptModule</class>
    <class kind="class">asIScriptContext</class>
  </compound>
  <compound kind="group">
    <name>api_secondary_interfaces</name>
    <title>Secondary interfaces</title>
    <filename>group__api__secondary__interfaces.html</filename>
    <class kind="class">asIStringFactory</class>
    <class kind="class">asIScriptGeneric</class>
    <class kind="class">asIScriptObject</class>
    <class kind="class">asITypeInfo</class>
    <class kind="class">asIScriptFunction</class>
  </compound>
  <compound kind="group">
    <name>api_auxiliary_interfaces</name>
    <title>Auxiliary interfaces</title>
    <filename>group__api__auxiliary__interfaces.html</filename>
    <class kind="class">asIThreadManager</class>
    <class kind="class">asIBinaryStream</class>
    <class kind="class">asILockableSharedBool</class>
    <class kind="class">asIJITCompilerAbstract</class>
    <class kind="class">asIJITCompiler</class>
    <class kind="class">asIJITCompilerV2</class>
  </compound>
  <compound kind="group">
    <name>api_principal_functions</name>
    <title>Principal functions</title>
    <filename>group__api__principal__functions.html</filename>
    <member kind="function">
      <type>AS_API asIScriptEngine *</type>
      <name>asCreateScriptEngine</name>
      <anchorfile>group__api__principal__functions.html</anchorfile>
      <anchor>ga030e559c6ec9e2ee0be5836b84061bac</anchor>
      <arglist>(asDWORD version=ANGELSCRIPT_VERSION)</arglist>
    </member>
    <member kind="function">
      <type>AS_API asIScriptContext *</type>
      <name>asGetActiveContext</name>
      <anchorfile>group__api__principal__functions.html</anchorfile>
      <anchor>gaf8b78824db10b0f757990805a4e18f70</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>asUINT</type>
      <name>asGetTypeTraits</name>
      <anchorfile>group__api__principal__functions.html</anchorfile>
      <anchor>ga863f2a1e60e6c19eea9c6b34690dcc00</anchor>
      <arglist>()</arglist>
    </member>
  </compound>
  <compound kind="group">
    <name>api_memory_functions</name>
    <title>Memory functions</title>
    <filename>group__api__memory__functions.html</filename>
    <member kind="function">
      <type>AS_API int</type>
      <name>asSetGlobalMemoryFunctions</name>
      <anchorfile>group__api__memory__functions.html</anchorfile>
      <anchor>ga527ab125defc58aa40cc151a25582a31</anchor>
      <arglist>(asALLOCFUNC_t allocFunc, asFREEFUNC_t freeFunc)</arglist>
    </member>
    <member kind="function">
      <type>AS_API int</type>
      <name>asResetGlobalMemoryFunctions</name>
      <anchorfile>group__api__memory__functions.html</anchorfile>
      <anchor>ga9267c4ad35aceaf7cc0961cd42147ee7</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API void *</type>
      <name>asAllocMem</name>
      <anchorfile>group__api__memory__functions.html</anchorfile>
      <anchor>ga8d26e33db9ce5a9dd61cf7ee2fe3cd23</anchor>
      <arglist>(size_t size)</arglist>
    </member>
    <member kind="function">
      <type>AS_API void</type>
      <name>asFreeMem</name>
      <anchorfile>group__api__memory__functions.html</anchorfile>
      <anchor>ga9da61275bbfd5f7bd55ed411d05fe103</anchor>
      <arglist>(void *mem)</arglist>
    </member>
  </compound>
  <compound kind="group">
    <name>api_multithread_functions</name>
    <title>Multi-thread support functions</title>
    <filename>group__api__multithread__functions.html</filename>
    <member kind="function">
      <type>AS_API int</type>
      <name>asPrepareMultithread</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>gaa5bea65c3f2a224bb1c677515e3bb0e2</anchor>
      <arglist>(asIThreadManager *externalMgr=0)</arglist>
    </member>
    <member kind="function">
      <type>AS_API void</type>
      <name>asUnprepareMultithread</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga011355a8978d438cec77b4e1f041cba7</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API asIThreadManager *</type>
      <name>asGetThreadManager</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga72f005266d6b5258c1dbe10449c78d24</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API void</type>
      <name>asAcquireExclusiveLock</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga016dbf716a1c761b3f903b92eb8bb580</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API void</type>
      <name>asReleaseExclusiveLock</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga8a0617637eea3d76e33a52758b2cd49f</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API void</type>
      <name>asAcquireSharedLock</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>gaa45545a038adcc8c73348cfe9488f32d</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API void</type>
      <name>asReleaseSharedLock</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga44f7327c5601e8dbf74768a2f3cc0dc3</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API int</type>
      <name>asAtomicInc</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>gaf0074d581ac2edd06e63e56e4be52c8e</anchor>
      <arglist>(int &amp;value)</arglist>
    </member>
    <member kind="function">
      <type>AS_API int</type>
      <name>asAtomicDec</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga0565bcb53be170dd85ae27a5b6f2b828</anchor>
      <arglist>(int &amp;value)</arglist>
    </member>
    <member kind="function">
      <type>AS_API int</type>
      <name>asThreadCleanup</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>ga51079811680d5217046aad2a2b695dc7</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API asILockableSharedBool *</type>
      <name>asCreateLockableSharedBool</name>
      <anchorfile>group__api__multithread__functions.html</anchorfile>
      <anchor>gaf192ffe898295aff4a76d8f2ced70034</anchor>
      <arglist>()</arglist>
    </member>
  </compound>
  <compound kind="group">
    <name>api_auxiliary_functions</name>
    <title>Auxiliary functions</title>
    <filename>group__api__auxiliary__functions.html</filename>
    <member kind="function">
      <type>AS_API const char *</type>
      <name>asGetLibraryVersion</name>
      <anchorfile>group__api__auxiliary__functions.html</anchorfile>
      <anchor>ga29ff21a8c90ba10bcbc848d205c52d68</anchor>
      <arglist>()</arglist>
    </member>
    <member kind="function">
      <type>AS_API const char *</type>
      <name>asGetLibraryOptions</name>
      <anchorfile>group__api__auxiliary__functions.html</anchorfile>
      <anchor>gae1c91abaa2ea97bc2b82ba5e07d3db8b</anchor>
      <arglist>()</arglist>
    </member>
  </compound>
  <compound kind="page">
    <name>doc_addon</name>
    <title>Add-ons</title>
    <filename>doc_addon.html</filename>
    <subpage>doc_addon_application.html</subpage>
    <subpage>doc_addon_script.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_addon_application</name>
    <title>Application modules</title>
    <filename>doc_addon_application.html</filename>
    <subpage>doc_addon_build.html</subpage>
    <subpage>doc_addon_ctxmgr.html</subpage>
    <subpage>doc_addon_debugger.html</subpage>
    <subpage>doc_addon_serializer.html</subpage>
    <subpage>doc_addon_helpers.html</subpage>
    <subpage>doc_addon_autowrap.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_addon_script</name>
    <title>Script extensions</title>
    <filename>doc_addon_script.html</filename>
    <subpage>doc_addon_std_string.html</subpage>
    <subpage>doc_addon_array.html</subpage>
    <subpage>doc_addon_any.html</subpage>
    <subpage>doc_addon_handle.html</subpage>
    <subpage>doc_addon_weakref.html</subpage>
    <subpage>doc_addon_dict.html</subpage>
    <subpage>doc_addon_file.html</subpage>
    <subpage>doc_addon_filesystem.html</subpage>
    <subpage>doc_addon_math.html</subpage>
    <subpage>doc_addon_grid.html</subpage>
    <subpage>doc_addon_datetime.html</subpage>
    <subpage>doc_addon_helpers_try.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_addon_datetime</name>
    <title>datetime object</title>
    <filename>doc_addon_datetime.html</filename>
    <docanchor file="doc_addon_datetime.html" title="Public C++ interface">doc_addon_datetime_1</docanchor>
    <docanchor file="doc_addon_datetime.html" title="Public script interface">doc_addon_datetime_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_serializer</name>
    <title>Serializer</title>
    <filename>doc_addon_serializer.html</filename>
    <docanchor file="doc_addon_serializer.html" title="Public C++ interface">doc_addon_serializer_1</docanchor>
    <docanchor file="doc_addon_serializer.html" title="Example usage">doc_addon_serializer_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_debugger</name>
    <title>Debugger</title>
    <filename>doc_addon_debugger.html</filename>
    <docanchor file="doc_addon_debugger.html" title="Public C++ interface">doc_addon_debugger_1</docanchor>
    <docanchor file="doc_addon_debugger.html" title="Example usage">doc_addon_debugger_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_ctxmgr</name>
    <title>Context manager</title>
    <filename>doc_addon_ctxmgr.html</filename>
    <docanchor file="doc_addon_ctxmgr.html" title="Public C++ interface">doc_addon_ctxmgr_1</docanchor>
    <docanchor file="doc_addon_ctxmgr.html" title="Public script interface">doc_addon_ctxmgr_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_array</name>
    <title>array template object</title>
    <filename>doc_addon_array.html</filename>
    <docanchor file="doc_addon_array.html" title="Public C++ interface">doc_addon_array_1</docanchor>
    <docanchor file="doc_addon_array.html" title="Public script interface">doc_addon_array_2</docanchor>
    <docanchor file="doc_addon_array.html" title="C++ example">doc_addon_array_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_grid</name>
    <title>grid template object</title>
    <filename>doc_addon_grid.html</filename>
    <docanchor file="doc_addon_grid.html" title="Public C++ interface">doc_addon_grid_1</docanchor>
    <docanchor file="doc_addon_grid.html" title="Public script interface">doc_addon_grid_2</docanchor>
    <docanchor file="doc_addon_grid.html" title="Example usage in script">doc_addon_grid_3</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_any</name>
    <title>any object</title>
    <filename>doc_addon_any.html</filename>
    <docanchor file="doc_addon_any.html" title="Public C++ interface">doc_addon_any_1</docanchor>
    <docanchor file="doc_addon_any.html" title="Public script interface">doc_addon_any_2</docanchor>
    <docanchor file="doc_addon_any.html" title="Example usage">doc_addon_any_3</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_handle</name>
    <title>ref object</title>
    <filename>doc_addon_handle.html</filename>
    <docanchor file="doc_addon_handle.html" title="Public C++ interface">doc_addon_handle_1</docanchor>
    <docanchor file="doc_addon_handle.html" title="Public script interface">doc_addon_handle_3</docanchor>
    <docanchor file="doc_addon_handle.html" title="Example usage from C++">doc_addon_handle_4</docanchor>
    <docanchor file="doc_addon_handle.html" title="Example on how to return a newly registered function from C++">doc_addon_handle_5</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_weakref</name>
    <title>weakref object</title>
    <filename>doc_addon_weakref.html</filename>
    <docanchor file="doc_addon_weakref.html" title="Public C++ interface">doc_addon_weakref_1</docanchor>
    <docanchor file="doc_addon_weakref.html" title="Public script interface">doc_addon_weakref_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_std_string</name>
    <title>string object</title>
    <filename>doc_addon_std_string.html</filename>
    <docanchor file="doc_addon_std_string.html" title="Public C++ interface">doc_addon_std_string_1</docanchor>
    <docanchor file="doc_addon_std_string.html" title="Public script interface">doc_addon_std_string_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_dict</name>
    <title>dictionary object</title>
    <filename>doc_addon_dict.html</filename>
    <docanchor file="doc_addon_dict.html" title="Public C++ interface">doc_addon_dict_1</docanchor>
    <docanchor file="doc_addon_dict.html" title="Public script interface">doc_addon_dict_2</docanchor>
    <docanchor file="doc_addon_dict.html" title="Example usage from C++">doc_addon_dict_3</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_file</name>
    <title>file object</title>
    <filename>doc_addon_file.html</filename>
    <docanchor file="doc_addon_file.html" title="Public C++ interface">doc_addon_file_1</docanchor>
    <docanchor file="doc_addon_file.html" title="Public script interface">doc_addon_file_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_filesystem</name>
    <title>filesystem object</title>
    <filename>doc_addon_filesystem.html</filename>
    <docanchor file="doc_addon_filesystem.html" title="Public C++ interface">doc_addon_filesystem_1</docanchor>
    <docanchor file="doc_addon_filesystem.html" title="Public script interface">doc_addon_filesystem_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_math</name>
    <title>math functions</title>
    <filename>doc_addon_math.html</filename>
    <docanchor file="doc_addon_math.html" title="Public script interface">doc_addon_math_1</docanchor>
    <docanchor file="doc_addon_math.html" title="Functions">doc_addon_math_funcs</docanchor>
    <docanchor file="doc_addon_math.html" title="The complex type">doc_addon_math_complex</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_build</name>
    <title>Script builder</title>
    <filename>doc_addon_build.html</filename>
    <docanchor file="doc_addon_build.html" title="Public C++ interface">doc_addon_build_1</docanchor>
    <docanchor file="doc_addon_build.html" title="The include callback signature">doc_addon_build_1_1</docanchor>
    <docanchor file="doc_addon_build.html" title="The pragma callback signature">doc_addon_build_1_2</docanchor>
    <docanchor file="doc_addon_build.html" title="Include directives">doc_addon_build_2</docanchor>
    <docanchor file="doc_addon_build.html" title="Conditional programming">doc_addon_build_condition</docanchor>
    <docanchor file="doc_addon_build.html" title="Metadata in scripts">doc_addon_build_metadata</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_autowrap</name>
    <title>Automatic wrapper functions</title>
    <filename>doc_addon_autowrap.html</filename>
    <docanchor file="doc_addon_autowrap.html" title="Example usage">doc_addon_autowrap_1</docanchor>
    <docanchor file="doc_addon_autowrap.html" title="Adding support for more parameters">doc_addon_autowrap_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_helpers_try</name>
    <title>Exception routines</title>
    <filename>doc_addon_helpers_try.html</filename>
    <docanchor file="doc_addon_helpers_try.html" title="Public C++ interface">doc_addon_helpers_try_1</docanchor>
    <docanchor file="doc_addon_helpers_try.html" title="Public script interface">doc_add_helpers_try_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_addon_helpers</name>
    <title>Helper functions</title>
    <filename>doc_addon_helpers.html</filename>
    <docanchor file="doc_addon_helpers.html" title="Public C++ interface">doc_addon_helpers_1</docanchor>
    <docanchor file="doc_addon_helpers.html" title="Example">doc_addon_helpers_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_access_mask</name>
    <title>Access masks and exposing different interfaces</title>
    <filename>doc_adv_access_mask.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_class_hierarchy</name>
    <title>Class hierarchies</title>
    <filename>doc_adv_class_hierarchy.html</filename>
    <docanchor file="doc_adv_class_hierarchy.html" title="Establishing the relationship">doc_adv_class_hierarchy_1</docanchor>
    <docanchor file="doc_adv_class_hierarchy.html" title="Inherited methods and properties">doc_adv_class_hierarchy_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_concurrent</name>
    <title>Concurrent scripts</title>
    <filename>doc_adv_concurrent.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_coroutine</name>
    <title>Co-routines</title>
    <filename>doc_adv_coroutine.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_custom_options</name>
    <title>Custom options</title>
    <filename>doc_adv_custom_options.html</filename>
    <docanchor file="doc_adv_custom_options.html" title="Registerable types">doc_adv_custom_options_reg_types</docanchor>
    <docanchor file="doc_adv_custom_options.html" title="Language modifications">doc_adv_custom_options_lang_mod</docanchor>
    <docanchor file="doc_adv_custom_options.html" title="Engine behaviours">doc_adv_custom_options_engine</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_dynamic_build</name>
    <title>Dynamic compilations</title>
    <filename>doc_adv_dynamic_build.html</filename>
    <docanchor file="doc_adv_dynamic_build.html" title="On demand builds">doc_adv_dynamic_build_ondemand</docanchor>
    <docanchor file="doc_adv_dynamic_build.html" title="Incremental builds">doc_adv_dynamic_build_incr</docanchor>
    <docanchor file="doc_adv_dynamic_build.html" title="Hot reloading scripts">doc_adv_dynamic_build_hot</docanchor>
    <docanchor file="doc_adv_dynamic_build.html" title="Things to consider">doc_adv_dynamic_build_hot_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_dynamic_config</name>
    <title>Dynamic configurations</title>
    <filename>doc_adv_dynamic_config.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_generic_handle</name>
    <title>Registering a generic handle type</title>
    <filename>doc_adv_generic_handle.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_import</name>
    <title>Import functions</title>
    <filename>doc_adv_import.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_inheritappclass</name>
    <title>Inheriting from application registered class</title>
    <filename>doc_adv_inheritappclass.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_jit</name>
    <title>How to build a JIT compiler</title>
    <filename>doc_adv_jit.html</filename>
    <docanchor file="doc_adv_jit.html" title="The JIT interface version 1">doc_adv_jit_v1</docanchor>
    <docanchor file="doc_adv_jit.html" title="The JIT interface version 2">doc_adv_jit_v2</docanchor>
    <docanchor file="doc_adv_jit.html" title="The structure of the JIT function">doc_adv_jit_3</docanchor>
    <docanchor file="doc_adv_jit.html" title="Traversing the byte code">doc_adv_jit_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_jit_1</name>
    <title>Byte code instructions</title>
    <filename>doc_adv_jit_1.html</filename>
    <docanchor file="doc_adv_jit_1.html" title="Object management">doc_adv_jit_1_1</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Math instructions">doc_adv_jit_1_2</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Bitwise instructions">doc_adv_jit_1_3</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Comparisons">doc_adv_jit_1_4</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Type conversions">doc_adv_jit_1_5</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Increment and decrement">doc_adv_jit_1_6</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Flow control">doc_adv_jit_1_7</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Stack and data management">doc_adv_jit_1_8</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Global variables">doc_adv_jit_1_9</docanchor>
    <docanchor file="doc_adv_jit_1.html" title="Initialization list management">doc_adv_jit_1_10</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_multithread</name>
    <title>Multithreading</title>
    <filename>doc_adv_multithread.html</filename>
    <docanchor file="doc_adv_multithread.html" title="Things to think about with a multithreaded environment">doc_adv_multithread_1</docanchor>
    <docanchor file="doc_adv_multithread.html" title="Fibers">doc_adv_fibers</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_namespace</name>
    <title>Using namespaces</title>
    <filename>doc_adv_namespace.html</filename>
    <docanchor file="doc_adv_namespace.html" title="Registering the interface with namespaces">doc_adv_namespace_reg</docanchor>
    <docanchor file="doc_adv_namespace.html" title="Finding entities in namespaces">doc_adv_namespace_enum</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_precompile</name>
    <title>Pre-compiled byte code</title>
    <filename>doc_adv_precompile.html</filename>
    <docanchor file="doc_adv_precompile.html" title="Things to remember">doc_adv_precompile_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_reflection</name>
    <title>Reflection</title>
    <filename>doc_adv_reflection.html</filename>
    <docanchor file="doc_adv_reflection.html" title="Enumerating variables and properties">doc_adv_reflection_vars</docanchor>
    <docanchor file="doc_adv_reflection.html" title="Enumerating functions and methods">doc_adv_reflection_funcs</docanchor>
    <docanchor file="doc_adv_reflection.html" title="Enumerating types">doc_adv_reflection_types</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_scoped_type</name>
    <title>Registering a scoped reference type</title>
    <filename>doc_adv_scoped_type.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_single_ref_type</name>
    <title>Registering a single-reference type</title>
    <filename>doc_adv_single_ref_type.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_adv_template</name>
    <title>Template types</title>
    <filename>doc_adv_template.html</filename>
    <docanchor file="doc_adv_template.html" title="Registering the template type">doc_adv_template_1</docanchor>
    <docanchor file="doc_adv_template.html" title="On subtype replacement for template instances">doc_adv_template_1_1</docanchor>
    <docanchor file="doc_adv_template.html" title="Validating template instantiations at compile time">doc_adv_template_4</docanchor>
    <docanchor file="doc_adv_template.html" title="Template specializations">doc_adv_template_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_timeout</name>
    <title>Timeout long running scripts</title>
    <filename>doc_adv_timeout.html</filename>
    <docanchor file="doc_adv_timeout.html" title="With the line callback">doc_adv_timeout_1</docanchor>
    <docanchor file="doc_adv_timeout.html" title="With a secondary thread">doc_adv_timeout_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_var_type</name>
    <title>The variable parameter type</title>
    <filename>doc_adv_var_type.html</filename>
    <docanchor file="doc_adv_var_type.html" title="Variable conversion operators">doc_adv_var_type_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_adv_weakref</name>
    <title>Weak references</title>
    <filename>doc_adv_weakref.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_arrays</name>
    <title>Custom array type</title>
    <filename>doc_arrays.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_as_vs_cpp_types</name>
    <title>Datatypes in AngelScript and C++</title>
    <filename>doc_as_vs_cpp_types.html</filename>
    <docanchor file="doc_as_vs_cpp_types.html" title="Primitives">doc_as_vs_cpp_types_1</docanchor>
    <docanchor file="doc_as_vs_cpp_types.html" title="Strings">doc_as_vs_cpp_types_5</docanchor>
    <docanchor file="doc_as_vs_cpp_types.html" title="Arrays">doc_as_vs_cpp_types_2</docanchor>
    <docanchor file="doc_as_vs_cpp_types.html" title="Object handles">doc_as_vs_cpp_types_3</docanchor>
    <docanchor file="doc_as_vs_cpp_types.html" title="Script classes and interfaces">doc_as_vc_cpp_types_5</docanchor>
    <docanchor file="doc_as_vs_cpp_types.html" title="Function pointers">doc_as_vc_cpp_types_6</docanchor>
    <docanchor file="doc_as_vs_cpp_types.html" title="Parameter references">doc_as_vs_cpp_types_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_call_script_func</name>
    <title>Calling a script function</title>
    <filename>doc_call_script_func.html</filename>
    <docanchor file="doc_call_script_func.html" title="Preparing context and executing the function">doc_call_script_1</docanchor>
    <docanchor file="doc_call_script_func.html" title="Passing and returning primitives">doc_call_script_2</docanchor>
    <docanchor file="doc_call_script_func.html" title="Passing and returning objects">doc_call_script_3</docanchor>
    <docanchor file="doc_call_script_func.html" title="Exception handling">doc_call_script_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_callbacks</name>
    <title>Funcdefs and script callback functions</title>
    <filename>doc_callbacks.html</filename>
    <docanchor file="doc_callbacks.html" title="An example">doc_callbacks_example</docanchor>
    <docanchor file="doc_callbacks.html" title="Delegates">doc_callbacks_delegate</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_compile_lib</name>
    <title>Compile the library</title>
    <filename>doc_compile_lib.html</filename>
    <docanchor file="doc_compile_lib.html" title="Set compile time options">doc_compile_lib_1</docanchor>
    <docanchor file="doc_compile_lib.html" title="Linking with the library">doc_compile_lib_2</docanchor>
    <docanchor file="doc_compile_lib.html" title="1. Include library source files in project">doc_compile_lib_2_1</docanchor>
    <docanchor file="doc_compile_lib.html" title="2. Compile a static library and link into project">doc_compile_lib_2_2</docanchor>
    <docanchor file="doc_compile_lib.html" title="3. Compile a dynamically loaded library with an import library">doc_compile_lib_2_3</docanchor>
    <docanchor file="doc_compile_lib.html" title="4. Load the dynamically loaded library manually">doc_compile_lib_2_4</docanchor>
    <docanchor file="doc_compile_lib.html" title="Considerations for specific platforms">doc_compile_platforms</docanchor>
    <docanchor file="doc_compile_lib.html" title="Windows 64bit">doc_compile_win64</docanchor>
    <docanchor file="doc_compile_lib.html" title="Microsoft Visual C++">doc_compile_msvc_sdk</docanchor>
    <docanchor file="doc_compile_lib.html" title="GNUC based compilers">doc_compile_gnuc</docanchor>
    <docanchor file="doc_compile_lib.html" title="Pocket PC with ARM CPU">doc_compile_pocketpc</docanchor>
    <docanchor file="doc_compile_lib.html" title="Marmalade">doc_compile_marmalade</docanchor>
    <docanchor file="doc_compile_lib.html" title="Size of the library">doc_compile_size</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_compile_script</name>
    <title>Compiling scripts</title>
    <filename>doc_compile_script.html</filename>
    <docanchor file="doc_compile_script.html" title="Message callback">doc_compile_script_msg</docanchor>
    <docanchor file="doc_compile_script.html" title="Loading and compiling scripts">doc_compile_script_load</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_cpp_exceptions</name>
    <title>C++ exceptions and longjmp</title>
    <filename>doc_cpp_exceptions.html</filename>
    <docanchor file="doc_cpp_exceptions.html" title="Exceptions">doc_cpp_exceptions_1</docanchor>
    <docanchor file="doc_cpp_exceptions.html" title="longjmp">doc_cpp_exceptions_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_debug</name>
    <title>Debugging scripts</title>
    <filename>doc_debug.html</filename>
    <docanchor file="doc_debug.html" title="Setting line breaks">doc_debug_1</docanchor>
    <docanchor file="doc_debug.html" title="Viewing the call stack">doc_debug_2</docanchor>
    <docanchor file="doc_debug.html" title="Inspecting variables">doc_debug_3</docanchor>
    <docanchor file="doc_debug.html" title="Debugging internally executed scripts">doc_debug_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_finetuning</name>
    <title>Fine tuning</title>
    <filename>doc_finetuning.html</filename>
    <docanchor file="doc_finetuning.html" title="Cache the functions and types">doc_finetuning_1</docanchor>
    <docanchor file="doc_finetuning.html" title="Reuse the context object">doc_finetuning_2</docanchor>
    <docanchor file="doc_finetuning.html" title="Context pool">doc_finetuning_2_1</docanchor>
    <docanchor file="doc_finetuning.html" title="Nested calls">doc_finetuning_2_2</docanchor>
    <docanchor file="doc_finetuning.html" title="Compile scripts without line cues">doc_finetuning_3</docanchor>
    <docanchor file="doc_finetuning.html" title="Disable thread safety">doc_finetuning_4</docanchor>
    <docanchor file="doc_finetuning.html" title="Turn off automatic garbage collection">doc_finetuning_5</docanchor>
    <docanchor file="doc_finetuning.html" title="Compare native calling convention versus generic calling convention">doc_finetuning_6</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_gc</name>
    <title>Garbage collection</title>
    <filename>doc_gc.html</filename>
    <docanchor file="doc_gc.html" title="Callback for detected circular references">doc_gc_circcallback</docanchor>
    <docanchor file="doc_gc.html" title="Garbage collection and multi-threading">doc_gc_threads</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_gc_object</name>
    <title>Garbage collected objects</title>
    <filename>doc_gc_object.html</filename>
    <docanchor file="doc_gc_object.html" title="GC support behaviours">doc_reg_gcref_1</docanchor>
    <docanchor file="doc_gc_object.html" title="Factory for garbage collection">doc_reg_gcref_2</docanchor>
    <docanchor file="doc_gc_object.html" title="Addref and release for garbage collection">doc_reg_gcref_3</docanchor>
    <docanchor file="doc_gc_object.html" title="GC behaviours for value types">doc_reg_gcref_value</docanchor>
    <docanchor file="doc_gc_object.html" title="Garbage collected objects and multi-threading">doc_reg_gcref_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_generic</name>
    <title>The generic calling convention</title>
    <filename>doc_generic.html</filename>
    <docanchor file="doc_generic.html" title="Extracting function arguments">doc_generic_1</docanchor>
    <docanchor file="doc_generic.html" title="Returning values">doc_generic_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_good_practice</name>
    <title>Good practices</title>
    <filename>doc_good_practice.html</filename>
    <docanchor file="doc_good_practice.html" title="Always check return values for registrations">doc_checkretval</docanchor>
    <docanchor file="doc_good_practice.html" title="Use the message callback to receive detailed error messages">doc_usemsgcallbck</docanchor>
    <docanchor file="doc_good_practice.html" title="Always verify return value after executing script function">doc_checkexceptions</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_hello_world</name>
    <title>Your first script</title>
    <filename>doc_hello_world.html</filename>
    <docanchor file="doc_hello_world.html" title="Helper functions">doc_hello_world_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_license</name>
    <title>License</title>
    <filename>doc_license.html</filename>
    <docanchor file="doc_license.html" title="AngelCode Scripting Library">doc_lic_1</docanchor>
  </compound>
  <compound kind="page">
    <name>main_topics</name>
    <title>Developer manual</title>
    <filename>main_topics.html</filename>
    <subpage>doc_license.html</subpage>
    <subpage>doc_start.html</subpage>
    <subpage>doc_understanding_as.html</subpage>
    <subpage>doc_register_api_topic.html</subpage>
    <subpage>doc_compile_script.html</subpage>
    <subpage>doc_call_script_func.html</subpage>
    <subpage>doc_use_script_class.html</subpage>
    <subpage>doc_callbacks.html</subpage>
    <subpage>doc_advanced.html</subpage>
    <subpage>doc_samples.html</subpage>
    <subpage>doc_addon.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_start</name>
    <title>Getting started</title>
    <filename>doc_start.html</filename>
    <subpage>doc_overview.html</subpage>
    <subpage>doc_compile_lib.html</subpage>
    <subpage>doc_hello_world.html</subpage>
    <subpage>doc_good_practice.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_register_api_topic</name>
    <title>Registering the application interface</title>
    <filename>doc_register_api_topic.html</filename>
    <subpage>doc_register_api.html</subpage>
    <subpage>doc_register_func.html</subpage>
    <subpage>doc_register_prop.html</subpage>
    <subpage>doc_register_type.html</subpage>
    <subpage>doc_advanced_api.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_advanced_api</name>
    <title>Advanced application interface</title>
    <filename>doc_advanced_api.html</filename>
    <subpage>doc_strings.html</subpage>
    <subpage>doc_arrays.html</subpage>
    <subpage>doc_gc_object.html</subpage>
    <subpage>doc_generic.html</subpage>
    <subpage>doc_adv_generic_handle.html</subpage>
    <subpage>doc_adv_scoped_type.html</subpage>
    <subpage>doc_adv_single_ref_type.html</subpage>
    <subpage>doc_adv_class_hierarchy.html</subpage>
    <subpage>doc_adv_var_type.html</subpage>
    <subpage>doc_adv_template.html</subpage>
    <subpage>doc_adv_weakref.html</subpage>
    <subpage>doc_cpp_exceptions.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_advanced</name>
    <title>Advanced topics</title>
    <filename>doc_advanced.html</filename>
    <subpage>doc_debug.html</subpage>
    <subpage>doc_adv_timeout.html</subpage>
    <subpage>doc_gc.html</subpage>
    <subpage>doc_adv_multithread.html</subpage>
    <subpage>doc_adv_concurrent.html</subpage>
    <subpage>doc_adv_coroutine.html</subpage>
    <subpage>doc_adv_import.html</subpage>
    <subpage>doc_adv_dynamic_build.html</subpage>
    <subpage>doc_adv_precompile.html</subpage>
    <subpage>doc_finetuning.html</subpage>
    <subpage>doc_adv_access_mask.html</subpage>
    <subpage>doc_adv_namespace.html</subpage>
    <subpage>doc_adv_dynamic_config.html</subpage>
    <subpage>doc_adv_custom_options.html</subpage>
    <subpage>doc_adv_reflection.html</subpage>
    <subpage>doc_serialization.html</subpage>
    <subpage>doc_adv_inheritappclass.html</subpage>
    <subpage>doc_adv_jit_topic.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_adv_jit_topic</name>
    <title>JIT compilation</title>
    <filename>doc_adv_jit_topic.html</filename>
    <subpage>doc_adv_jit.html</subpage>
    <subpage>doc_adv_jit_1.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_script</name>
    <title>The script language</title>
    <filename>doc_script.html</filename>
    <subpage>doc_script_global.html</subpage>
    <subpage>doc_script_statements.html</subpage>
    <subpage>doc_expressions.html</subpage>
    <subpage>doc_datatypes.html</subpage>
    <subpage>doc_script_func.html</subpage>
    <subpage>doc_script_class.html</subpage>
    <subpage>doc_script_handle.html</subpage>
    <subpage>doc_script_shared.html</subpage>
    <subpage>doc_operator_precedence.html</subpage>
    <subpage>doc_reserved_keywords.html</subpage>
    <subpage>doc_script_bnf.html</subpage>
    <subpage>doc_script_stdlib.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_script_global</name>
    <title>Global entities</title>
    <filename>doc_script_global.html</filename>
    <subpage>doc_global_func.html</subpage>
    <subpage>doc_global_variable.html</subpage>
    <subpage>doc_global_virtprop.html</subpage>
    <subpage>doc_global_class.html</subpage>
    <subpage>doc_global_interface.html</subpage>
    <subpage>doc_script_mixin.html</subpage>
    <subpage>doc_global_enums.html</subpage>
    <subpage>doc_global_funcdef.html</subpage>
    <subpage>doc_global_typedef.html</subpage>
    <subpage>doc_global_namespace.html</subpage>
    <subpage>doc_global_import.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_global_func</name>
    <title>Functions</title>
    <filename>doc_global_func.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_func</name>
    <title>Functions</title>
    <filename>doc_script_func.html</filename>
    <subpage>doc_script_func_decl.html</subpage>
    <subpage>doc_script_func_ref.html</subpage>
    <subpage>doc_script_func_retref.html</subpage>
    <subpage>doc_script_func_overload.html</subpage>
    <subpage>doc_script_func_defarg.html</subpage>
    <subpage>doc_script_anonfunc.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_global_class</name>
    <title>Script classes</title>
    <filename>doc_global_class.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_class</name>
    <title>Script classes</title>
    <filename>doc_script_class.html</filename>
    <subpage>doc_script_class_desc.html</subpage>
    <subpage>doc_script_class_construct.html</subpage>
    <subpage>doc_script_class_memberinit.html</subpage>
    <subpage>doc_script_class_destruct.html</subpage>
    <subpage>doc_script_class_methods.html</subpage>
    <subpage>doc_script_class_inheritance.html</subpage>
    <subpage>doc_script_class_private.html</subpage>
    <subpage>doc_script_class_ops.html</subpage>
    <subpage>doc_script_class_prop.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_memory</name>
    <title>Memory management</title>
    <filename>doc_memory.html</filename>
    <docanchor file="doc_memory.html" title="Overview of the memory management">doc_memory_1</docanchor>
    <docanchor file="doc_memory.html" title="Reference counting algorithm">doc_memory_2</docanchor>
    <docanchor file="doc_memory.html" title="Garbage collector algorithm">doc_memory_3</docanchor>
    <docanchor file="doc_memory.html" title="Memory heap">doc_memory_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_module</name>
    <title>Script modules</title>
    <filename>doc_module.html</filename>
    <docanchor file="doc_module.html" title="Single module versus multiple modules">doc_module_single_vs_multi</docanchor>
    <docanchor file="doc_module.html" title="Exchanging information between modules">doc_module_exchange</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_obj_handle</name>
    <title>Object handles to the application</title>
    <filename>doc_obj_handle.html</filename>
    <docanchor file="doc_obj_handle.html" title="Managing the reference counter in functions">doc_obj_handle_3</docanchor>
    <docanchor file="doc_obj_handle.html" title="Auto handles can make it easier">doc_obj_handle_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_overview</name>
    <title>Overview</title>
    <filename>doc_overview.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_register_api</name>
    <title>What can be registered</title>
    <filename>doc_register_api.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_register_func</name>
    <title>Registering a function</title>
    <filename>doc_register_func.html</filename>
    <docanchor file="doc_register_func.html" title="How to get the address of the application function or method">doc_register_func_1</docanchor>
    <docanchor file="doc_register_func.html" title="Calling convention">doc_register_func_2</docanchor>
    <docanchor file="doc_register_func.html" title="A little on type differences">doc_register_func_3</docanchor>
    <docanchor file="doc_register_func.html" title="Virtual inheritance is not supported">doc_register_func_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_register_prop</name>
    <title>Registering global properties</title>
    <filename>doc_register_prop.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_register_type</name>
    <title>Registering an object type</title>
    <filename>doc_register_type.html</filename>
    <subpage>doc_reg_basicref.html</subpage>
    <subpage>doc_register_val_type.html</subpage>
    <subpage>doc_reg_opbeh.html</subpage>
    <subpage>doc_reg_objmeth.html</subpage>
    <subpage>doc_reg_objprop.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_reg_basicref</name>
    <title>Registering a reference type</title>
    <filename>doc_reg_basicref.html</filename>
    <docanchor file="doc_reg_basicref.html" title="Factory function">doc_reg_basicref_1</docanchor>
    <docanchor file="doc_reg_basicref.html" title="Factory function with auxiliary object">doc_reg_basicref_1_1</docanchor>
    <docanchor file="doc_reg_basicref.html" title="List factory function">doc_reg_basicref_4</docanchor>
    <docanchor file="doc_reg_basicref.html" title="Addref and release behaviours">doc_reg_basicref_2</docanchor>
    <docanchor file="doc_reg_basicref.html" title="Reference types without reference counting">doc_reg_nocount</docanchor>
    <docanchor file="doc_reg_basicref.html" title="Registering an uninstantiable reference type">doc_reg_noinst</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_register_val_type</name>
    <title>Registering a value type</title>
    <filename>doc_register_val_type.html</filename>
    <docanchor file="doc_register_val_type.html" title="Constructor and destructor">doc_reg_val_1</docanchor>
    <docanchor file="doc_register_val_type.html" title="List constructor">doc_reg_val_3</docanchor>
    <docanchor file="doc_register_val_type.html" title="Value types and native calling conventions">doc_reg_val_2</docanchor>
    <docanchor file="doc_register_val_type.html" title="For compilers that don&apos;t support C++11 and asGetTypeTraits">doc_reg_val_2_nocpp11</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_reg_opbeh</name>
    <title>Registering operator behaviours</title>
    <filename>doc_reg_opbeh.html</filename>
    <docanchor file="doc_reg_opbeh.html" title="Operator overloads">doc_reg_opbeh_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_reg_objmeth</name>
    <title>Registering object methods</title>
    <filename>doc_reg_objmeth.html</filename>
    <docanchor file="doc_reg_objmeth.html" title="Composite members">doc_reg_objmeth_composite</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_reg_objprop</name>
    <title>Registering object properties</title>
    <filename>doc_reg_objprop.html</filename>
    <docanchor file="doc_reg_objprop.html" title="Composite members">doc_reg_objprop_composite</docanchor>
    <docanchor file="doc_reg_objprop.html" title="Property accessors">doc_reg_objprop_accessor</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_samples</name>
    <title>Samples</title>
    <filename>doc_samples.html</filename>
    <subpage>doc_samples_tutorial.html</subpage>
    <subpage>doc_samples_concurrent.html</subpage>
    <subpage>doc_samples_console.html</subpage>
    <subpage>doc_samples_corout.html</subpage>
    <subpage>doc_samples_events.html</subpage>
    <subpage>doc_samples_incl.html</subpage>
    <subpage>doc_samples_asbuild.html</subpage>
    <subpage>doc_samples_game.html</subpage>
    <subpage>doc_samples_asrun.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_samples_tutorial</name>
    <title>Tutorial</title>
    <filename>doc_samples_tutorial.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_concurrent</name>
    <title>Concurrent scripts</title>
    <filename>doc_samples_concurrent.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_console</name>
    <title>Console</title>
    <filename>doc_samples_console.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_corout</name>
    <title>Co-routines</title>
    <filename>doc_samples_corout.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_events</name>
    <title>Events</title>
    <filename>doc_samples_events.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_incl</name>
    <title>Include directive</title>
    <filename>doc_samples_incl.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_asbuild</name>
    <title>Generic compiler</title>
    <filename>doc_samples_asbuild.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_samples_asrun</name>
    <title>Command line runner</title>
    <filename>doc_samples_asrun.html</filename>
    <subpage>doc_samples_asrun_manual.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_samples_asrun_manual</name>
    <title>asrun manual</title>
    <filename>doc_samples_asrun_manual.html</filename>
    <docanchor file="doc_samples_asrun_manual.html" title="Usage">doc_samples_asrun_usage</docanchor>
    <docanchor file="doc_samples_asrun_manual.html" title="Scripts">doc_samples_asrun_script</docanchor>
    <docanchor file="doc_samples_asrun_manual.html" title="How to debug scripts">doc_samples_asrun_debug</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_samples_game</name>
    <title>Game</title>
    <filename>doc_samples_game.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_bnf</name>
    <title>Script language grammar</title>
    <filename>doc_script_bnf.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_class_desc</name>
    <title>Script class overview</title>
    <filename>doc_script_class_desc.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_class_construct</name>
    <title>Class constructors</title>
    <filename>doc_script_class_construct.html</filename>
    <docanchor file="doc_script_class_construct.html" title="Auto-generated constructors">doc_script_class_construct_auto</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_class_destruct</name>
    <title>Class destructor</title>
    <filename>doc_script_class_destruct.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_class_methods</name>
    <title>Class methods</title>
    <filename>doc_script_class_methods.html</filename>
    <docanchor file="doc_script_class_methods.html" title="Const methods">doc_script_class_const</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_class_inheritance</name>
    <title>Inheritance and polymorphism</title>
    <filename>doc_script_class_inheritance.html</filename>
    <docanchor file="doc_script_class_inheritance.html" title="Extra control with final, abstract, and override">doc_script_class_inheritance_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_class_private</name>
    <title>Protected and private class members</title>
    <filename>doc_script_class_private.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_class_memberinit</name>
    <title>Initialization of class members</title>
    <filename>doc_script_class_memberinit.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_class_ops</name>
    <title>Operator overloads</title>
    <filename>doc_script_class_ops.html</filename>
    <docanchor file="doc_script_class_ops.html" title="Prefixed unary operators">doc_script_class_unary_ops</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Postfixed unary operators">doc_script_class_unary2_ops</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Comparison operators">doc_script_class_cmp_ops</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Assignment operators">doc_script_class_assign_ops</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Auto-generated assignment operator">doc_script_class_assign_ops_auto</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Binary operators">doc_script_class_binary_ops</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Index operators">doc_script_class_index_op</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Functor operator">doc_script_class_call</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Type conversion operators">doc_script_class_conv</docanchor>
    <docanchor file="doc_script_class_ops.html" title="Foreach loop operators">doc_script_class_foreach_ops</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_class_prop</name>
    <title>Property accessors</title>
    <filename>doc_script_class_prop.html</filename>
    <docanchor file="doc_script_class_prop.html" title="Indexed property accessors">doc_script_class_prop_index</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes</name>
    <title>Data types</title>
    <filename>doc_datatypes.html</filename>
    <subpage>doc_datatypes_primitives.html</subpage>
    <subpage>doc_datatypes_obj.html</subpage>
    <subpage>doc_datatypes_funcptr.html</subpage>
    <subpage>doc_datatypes_strings.html</subpage>
    <subpage>doc_datatypes_auto.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_primitives</name>
    <title>Primitives</title>
    <filename>doc_datatypes_primitives.html</filename>
    <docanchor file="doc_datatypes_primitives.html" title="void">void</docanchor>
    <docanchor file="doc_datatypes_primitives.html" title="bool">bool</docanchor>
    <docanchor file="doc_datatypes_primitives.html" title="Integer numbers">int</docanchor>
    <docanchor file="doc_datatypes_primitives.html" title="Real numbers">real</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_obj</name>
    <title>Objects and handles</title>
    <filename>doc_datatypes_obj.html</filename>
    <docanchor file="doc_datatypes_obj.html" title="Objects">objects</docanchor>
    <docanchor file="doc_datatypes_obj.html" title="Object handles">handles</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_funcptr</name>
    <title>Function handles</title>
    <filename>doc_datatypes_funcptr.html</filename>
    <docanchor file="doc_datatypes_funcptr.html" title="Delegates">doc_datatypes_delegate</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_strings</name>
    <title>Strings</title>
    <filename>doc_datatypes_strings.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_auto</name>
    <title>Auto declarations</title>
    <filename>doc_datatypes_auto.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_expressions</name>
    <title>Expressions</title>
    <filename>doc_expressions.html</filename>
    <docanchor file="doc_expressions.html" title="Assignments">assignment</docanchor>
    <docanchor file="doc_expressions.html" title="Function call">function</docanchor>
    <docanchor file="doc_expressions.html" title="Math operators">math</docanchor>
    <docanchor file="doc_expressions.html" title="Bitwise operators">bits</docanchor>
    <docanchor file="doc_expressions.html" title="Compound assignments">compound</docanchor>
    <docanchor file="doc_expressions.html" title="Logic operators">logic</docanchor>
    <docanchor file="doc_expressions.html" title="Equality comparison operators">equal</docanchor>
    <docanchor file="doc_expressions.html" title="Relational comparison operators">relation</docanchor>
    <docanchor file="doc_expressions.html" title="Identity comparison operators">identity</docanchor>
    <docanchor file="doc_expressions.html" title="Increment operators">increment</docanchor>
    <docanchor file="doc_expressions.html" title="Indexing operator">opindex</docanchor>
    <docanchor file="doc_expressions.html" title="Conditional expression">condition</docanchor>
    <docanchor file="doc_expressions.html" title="Member access">member</docanchor>
    <docanchor file="doc_expressions.html" title="Handle-of">handle</docanchor>
    <docanchor file="doc_expressions.html" title="Parenthesis">parenthesis</docanchor>
    <docanchor file="doc_expressions.html" title="Scope resolution">scope</docanchor>
    <docanchor file="doc_expressions.html" title="Type conversions">conversion</docanchor>
    <docanchor file="doc_expressions.html" title="Anonymous objects">anonobj</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_func_decl</name>
    <title>Function declaration</title>
    <filename>doc_script_func_decl.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_func_ref</name>
    <title>Parameter references</title>
    <filename>doc_script_func_ref.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_func_retref</name>
    <title>Return references</title>
    <filename>doc_script_func_retref.html</filename>
    <docanchor file="doc_script_func_retref.html" title="References to global variables are allowed">doc_script_retref_global</docanchor>
    <docanchor file="doc_script_func_retref.html" title="References to class members are allowed">doc_script_refref_member</docanchor>
    <docanchor file="doc_script_func_retref.html" title="Can&apos;t return reference to local variables">doc_script_retref_local</docanchor>
    <docanchor file="doc_script_func_retref.html" title="Can&apos;t use expressions with deferred parameters">doc_script_retref_deferred</docanchor>
    <docanchor file="doc_script_func_retref.html" title="Can&apos;t use expressions that rely on local objects">doc_script_refref_cleanup</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_func_overload</name>
    <title>Function overloading</title>
    <filename>doc_script_func_overload.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_func_defarg</name>
    <title>Default arguments</title>
    <filename>doc_script_func_defarg.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_anonfunc</name>
    <title>Anonymous functions</title>
    <filename>doc_script_anonfunc.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_variable</name>
    <title>Variables</title>
    <filename>doc_global_variable.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_virtprop</name>
    <title>Virtual properties</title>
    <filename>doc_global_virtprop.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_interface</name>
    <title>Interfaces</title>
    <filename>doc_global_interface.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_import</name>
    <title>Imports</title>
    <filename>doc_global_import.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_enums</name>
    <title>Enums</title>
    <filename>doc_global_enums.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_typedef</name>
    <title>Typedefs</title>
    <filename>doc_global_typedef.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_funcdef</name>
    <title>Funcdefs</title>
    <filename>doc_global_funcdef.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_global_namespace</name>
    <title>Namespaces</title>
    <filename>doc_global_namespace.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_handle</name>
    <title>Object handles</title>
    <filename>doc_script_handle.html</filename>
    <docanchor file="doc_script_handle.html" title="General usage">doc_script_handle_1</docanchor>
    <docanchor file="doc_script_handle.html" title="Object life times">doc_script_handle_2</docanchor>
    <docanchor file="doc_script_handle.html" title="Object relations and polymorphing">doc_script_handle_3</docanchor>
    <docanchor file="doc_script_handle.html" title="Const handles">doc_script_handle_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_mixin</name>
    <title>Mixin class</title>
    <filename>doc_script_mixin.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_operator_precedence</name>
    <title>Operator precedence</title>
    <filename>doc_operator_precedence.html</filename>
    <docanchor file="doc_operator_precedence.html" title="Unary operators">unary</docanchor>
    <docanchor file="doc_operator_precedence.html" title="Binary and ternary operators">binary</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_reserved_keywords</name>
    <title>Reserved keywords and tokens</title>
    <filename>doc_reserved_keywords.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_script_shared</name>
    <title>Shared script entities</title>
    <filename>doc_script_shared.html</filename>
    <docanchor file="doc_script_shared.html" title="How to declare shared entities">doc_script_shared_1</docanchor>
    <docanchor file="doc_script_shared.html" title="External shared entities">doc_script_shared_external</docanchor>
    <docanchor file="doc_script_shared.html" title="What can be shared">doc_script_shared_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_statements</name>
    <title>Statements</title>
    <filename>doc_script_statements.html</filename>
    <docanchor file="doc_script_statements.html" title="Variable declarations">variable</docanchor>
    <docanchor file="doc_script_statements.html" title="Expression statement">expression</docanchor>
    <docanchor file="doc_script_statements.html" title="Conditions: if / if-else / switch-case">if</docanchor>
    <docanchor file="doc_script_statements.html" title="Loops: while / do-while / for / foreach">while</docanchor>
    <docanchor file="doc_script_statements.html" title="Loop control: break / continue">break</docanchor>
    <docanchor file="doc_script_statements.html" title="Return statement">return</docanchor>
    <docanchor file="doc_script_statements.html" title="Statement blocks">block</docanchor>
    <docanchor file="doc_script_statements.html" title="Try-catch blocks">try</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib</name>
    <title>Standard library</title>
    <filename>doc_script_stdlib.html</filename>
    <subpage>doc_script_stdlib_exception.html</subpage>
    <subpage>doc_script_stdlib_string.html</subpage>
    <subpage>doc_datatypes_arrays.html</subpage>
    <subpage>doc_datatypes_dictionary.html</subpage>
    <subpage>doc_datatypes_ref.html</subpage>
    <subpage>doc_datatypes_weakref.html</subpage>
    <subpage>doc_script_stdlib_datetime.html</subpage>
    <subpage>doc_script_stdlib_coroutine.html</subpage>
    <subpage>doc_script_stdlib_file.html</subpage>
    <subpage>doc_script_stdlib_filesystem.html</subpage>
    <subpage>doc_script_stdlib_system.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_exception</name>
    <title>Exception handling</title>
    <filename>doc_script_stdlib_exception.html</filename>
    <docanchor file="doc_script_stdlib_exception.html" title="Functions">try_func</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_arrays</name>
    <title>array</title>
    <filename>doc_datatypes_arrays.html</filename>
    <docanchor file="doc_datatypes_arrays.html" title="Supporting array object">doc_datatypes_arrays_addon</docanchor>
    <docanchor file="doc_datatypes_arrays.html" title="Operators">doc_datatypes_array_addon_ops</docanchor>
    <docanchor file="doc_datatypes_arrays.html" title="Methods">doc_datatypes_array_addon_mthd</docanchor>
    <docanchor file="doc_datatypes_arrays.html" title="Script example">doc_datatypes_array_addon_example</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_dictionary</name>
    <title>dictionary</title>
    <filename>doc_datatypes_dictionary.html</filename>
    <docanchor file="doc_datatypes_dictionary.html" title="Supporting dictionary object">doc_datatypes_dictionary_addon</docanchor>
    <docanchor file="doc_datatypes_dictionary.html" title="Operators">doc_datatypes_dictionary_addon_ops</docanchor>
    <docanchor file="doc_datatypes_dictionary.html" title="Methods">doc_datatypes_dictionary_addon_mthd</docanchor>
    <docanchor file="doc_datatypes_dictionary.html" title="Supporting dictionaryValue object">doc_datatypes_dictionaryValue_addon</docanchor>
    <docanchor file="doc_datatypes_dictionary.html" title="Operators">doc_datatypes_dictionaryValue_addon_ops</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_string</name>
    <title>string</title>
    <filename>doc_script_stdlib_string.html</filename>
    <docanchor file="doc_script_stdlib_string.html" title="Supporting string object and functions">doc_datatypes_strings_addon</docanchor>
    <docanchor file="doc_script_stdlib_string.html" title="Operators">doc_datatypes_strings_addon_ops</docanchor>
    <docanchor file="doc_script_stdlib_string.html" title="Methods">doc_datatypes_strings_addon_mthd</docanchor>
    <docanchor file="doc_script_stdlib_string.html" title="Functions">doc_datatypes_strings_addon_funcs</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_ref</name>
    <title>ref</title>
    <filename>doc_datatypes_ref.html</filename>
    <docanchor file="doc_datatypes_ref.html" title="Supporting ref object">doc_datatypes_ref_addon</docanchor>
    <docanchor file="doc_datatypes_ref.html" title="Operators">doc_datatypes_ref_addon_ops</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_datatypes_weakref</name>
    <title>weakref</title>
    <filename>doc_datatypes_weakref.html</filename>
    <docanchor file="doc_datatypes_weakref.html" title="Supporting weakref object">doc_datatypes_weakref_addon</docanchor>
    <docanchor file="doc_datatypes_weakref.html" title="Constructors">doc_datatypes_weakref_addon_construct</docanchor>
    <docanchor file="doc_datatypes_weakref.html" title="Operators">doc_datatypes_weakref_addon_ops</docanchor>
    <docanchor file="doc_datatypes_weakref.html" title="Methods">doc_datatypes_weakref_addon_mthd</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_datetime</name>
    <title>datetime</title>
    <filename>doc_script_stdlib_datetime.html</filename>
    <docanchor file="doc_script_stdlib_datetime.html" title="Supporting datetime object">doc_datatype_datetime_addon</docanchor>
    <docanchor file="doc_script_stdlib_datetime.html" title="Constructors">doc_addon_datetime_2_construct</docanchor>
    <docanchor file="doc_script_stdlib_datetime.html" title="Methods">doc_addon_datetime_2_methods</docanchor>
    <docanchor file="doc_script_stdlib_datetime.html" title="Operators">doc_addon_datetime_2_ops</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_coroutine</name>
    <title>Co-routines</title>
    <filename>doc_script_stdlib_coroutine.html</filename>
    <docanchor file="doc_script_stdlib_coroutine.html" title="Functions">doc_script_stdlib_coroutine_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_file</name>
    <title>file</title>
    <filename>doc_script_stdlib_file.html</filename>
    <docanchor file="doc_script_stdlib_file.html" title="Supporting file object">doc_script_stdlib_file_1</docanchor>
    <docanchor file="doc_script_stdlib_file.html" title="Methods">doc_script_stdlib_file_1_1</docanchor>
    <docanchor file="doc_script_stdlib_file.html" title="Properties">doc_script_stdlib_file_1_2</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_filesystem</name>
    <title>filesystem</title>
    <filename>doc_script_stdlib_filesystem.html</filename>
    <docanchor file="doc_script_stdlib_filesystem.html" title="Supporting filesystem object">doc_script_stdlib_filesystem_1</docanchor>
    <docanchor file="doc_script_stdlib_filesystem.html" title="Methods">doc_script_stdlib_filesystem_1_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_script_stdlib_system</name>
    <title>System functions</title>
    <filename>doc_script_stdlib_system.html</filename>
    <docanchor file="doc_script_stdlib_system.html" title="Functions">doc_script_stdlib_system_1</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_serialization</name>
    <title>Serialization</title>
    <filename>doc_serialization.html</filename>
    <docanchor file="doc_serialization.html" title="Serialization of modules">doc_serialization_modules</docanchor>
    <docanchor file="doc_serialization.html" title="Serialization of global variables">doc_serialization_vars</docanchor>
    <docanchor file="doc_serialization.html" title="Serialization of objects">doc_serialization_objects</docanchor>
    <docanchor file="doc_serialization.html" title="Serialization of contexts">doc_serialization_contexts</docanchor>
    <docanchor file="doc_serialization.html">Limitations</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_strings</name>
    <title>Custom string type</title>
    <filename>doc_strings.html</filename>
    <docanchor file="doc_strings.html" title="Registering the custom string type">doc_string_register</docanchor>
    <docanchor file="doc_strings.html" title="Unicode vs ASCII">doc_strings_1</docanchor>
    <docanchor file="doc_strings.html" title="Multiline string literals">doc_string_2</docanchor>
    <docanchor file="doc_strings.html" title="Character literals">doc_string_3</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_understanding_as</name>
    <title>Understanding AngelScript</title>
    <filename>doc_understanding_as.html</filename>
    <subpage>doc_versions.html</subpage>
    <subpage>doc_module.html</subpage>
    <subpage>doc_as_vs_cpp_types.html</subpage>
    <subpage>doc_obj_handle.html</subpage>
    <subpage>doc_memory.html</subpage>
    <subpage>doc_typeid.html</subpage>
  </compound>
  <compound kind="page">
    <name>doc_typeid</name>
    <title>Structure of the typeid</title>
    <filename>doc_typeid.html</filename>
  </compound>
  <compound kind="page">
    <name>doc_use_script_class</name>
    <title>Using script classes</title>
    <filename>doc_use_script_class.html</filename>
    <docanchor file="doc_use_script_class.html" title="Instantiating the script class">doc_use_script_class_1</docanchor>
    <docanchor file="doc_use_script_class.html" title="Calling a method on the script class">doc_use_script_class_2</docanchor>
    <docanchor file="doc_use_script_class.html" title="Receiving script classes">doc_use_script_class_3</docanchor>
    <docanchor file="doc_use_script_class.html" title="Returning script classes">doc_use_script_class_4</docanchor>
  </compound>
  <compound kind="page">
    <name>doc_versions</name>
    <title>Versions</title>
    <filename>doc_versions.html</filename>
    <docanchor file="doc_versions.html" title="History">doc_versions_milestones</docanchor>
    <docanchor file="doc_versions.html" title="2003 - Birth and first public release">doc_versions_2003</docanchor>
    <docanchor file="doc_versions.html" title="2005 - Version 2, sand box, object handles, script classes, and garbage collection">doc_versions_2005</docanchor>
    <docanchor file="doc_versions.html" title="2006 - Script interface">doc_versions_2006</docanchor>
    <docanchor file="doc_versions.html" title="2009 - Inheritance, template types, operator overloads, and JIT compilation">doc_versions_2009</docanchor>
    <docanchor file="doc_versions.html" title="2010 - Function pointers">doc_versions_2010</docanchor>
    <docanchor file="doc_versions.html" title="2011 - Automatic garbage collection and debugging">doc_versions_2011</docanchor>
    <docanchor file="doc_versions.html" title="2012 - Namespaces and mixins">doc_versions_2012</docanchor>
    <docanchor file="doc_versions.html" title="2013 - Improved template types, delegates, weak references, and initialization lists">doc_versions_2013</docanchor>
    <docanchor file="doc_versions.html" title="2014 - Named arguments and auto">doc_versions_2014</docanchor>
    <docanchor file="doc_versions.html" title="2015 - Anonymous functions">doc_versions_2015</docanchor>
    <docanchor file="doc_versions.html" title="2016 - Child funcdefs">doc_versions_2016</docanchor>
    <docanchor file="doc_versions.html" title="2017 - external keyword and anonymous initialization lists">doc_versions_2017</docanchor>
    <docanchor file="doc_versions.html" title="2018 - Try-catch statements and explicit constructors">doc_versions_2018</docanchor>
    <docanchor file="doc_versions.html" title="2019 - Explicit property keyword">doc_versions_2019</docanchor>
    <docanchor file="doc_versions.html" title="2022 - Serialization of the context stack">doc_versions_2022</docanchor>
    <docanchor file="doc_versions.html" title="2024 - Improved JIT compiler interface and auto generated copy constructors">doc_versions_2024</docanchor>
  </compound>
  <compound kind="page">
    <name>index</name>
    <title>Introduction</title>
    <filename>index.html</filename>
  </compound>
</tagfile>
