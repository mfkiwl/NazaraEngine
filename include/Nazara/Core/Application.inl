// Copyright (C) 2023 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Engine - Core module"
// For conditions of distribution and use, see copyright notice in Config.hpp

#include <Nazara/Core/Application.hpp>
#include <Nazara/Core/Debug.hpp>

namespace Nz
{
	namespace Detail
	{
		template<typename, typename, typename = void>
		struct ModuleHasRegister : std::false_type {};

		template<typename M, typename C>
		struct ModuleHasRegister<M, C, std::void_t<decltype(std::declval<M>().RegisterComponent(std::declval<C&>()))>> : std::true_type {};

		template<typename> struct ModuleRegisterer;

		template<typename Module, typename... Rest>
		struct ModuleRegisterer<TypeList<Module, Rest...>>
		{
			template<typename T, typename C>
			static void Register(T& modules, C& component)
			{
				if constexpr (ModuleHasRegister<Module, C>::value)
					modules.template Get<Module>().RegisterComponent(component);

				ModuleRegisterer<TypeList<Rest...>>::Register(modules, component);
			}
		};

		template<>
		struct ModuleRegisterer<TypeList<>>
		{
			template<typename T, typename C>
			static void Register(T& /*modules*/, C& /*component*/)
			{
			}
		};
	}

	template<typename... ModuleList>
	template<typename... ModuleConfig>
	Application<ModuleList...>::Application(ModuleConfig&&... configs) :
	m_modules(std::forward<ModuleConfig>(configs)...)
	{
	}

	template<typename... ModuleList>
	template<typename... ModuleConfig>
	Application<ModuleList...>::Application(int argc, char** argv, ModuleConfig&&... configs) :
	ApplicationBase(argc, argv),
	m_modules(std::forward<ModuleConfig>(configs)...)
	{
	}
	
	template<typename... ModuleList>
	template<typename... ModuleConfig>
	Application<ModuleList...>::Application(int argc, const Pointer<const char>* argv, ModuleConfig&&... configs) :
	ApplicationBase(argc, argv),
	m_modules(std::forward<ModuleConfig>(configs)...)
	{
	}

	template<typename... ModuleList>
	template<typename T, typename... Args>
	T& Application<ModuleList...>::AddComponent(Args&&... args)
	{
		T& component = ApplicationBase::AddComponent<T>(std::forward<Args>(args)...);
		Detail::ModuleRegisterer<typename decltype(m_modules)::ModuleTypeList>::template Register(m_modules, component);

		return component;
	}

	template<typename... ModuleList>
	Application<ModuleList...>::~Application()
	{
		// Clear components before releasing modules
		ClearComponents();
	}
}

#include <Nazara/Core/DebugOff.hpp>