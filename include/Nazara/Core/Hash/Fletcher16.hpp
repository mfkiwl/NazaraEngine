// Copyright (C) 2021 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Engine - Core module"
// For conditions of distribution and use, see copyright notice in Config.hpp

#pragma once

#ifndef NAZARA_CORE_HASH_FLETCHER16_HPP
#define NAZARA_CORE_HASH_FLETCHER16_HPP

#include <Nazara/Prerequisites.hpp>
#include <Nazara/Core/AbstractHash.hpp>
#include <Nazara/Core/ByteArray.hpp>

namespace Nz
{
	struct HashFletcher16_state;

	class NAZARA_CORE_API HashFletcher16 : public AbstractHash
	{
		public:
			HashFletcher16();
			virtual ~HashFletcher16();

			void Append(const UInt8* data, std::size_t len) override;
			void Begin() override;
			ByteArray End() override;

			std::size_t GetDigestLength() const override;
			const char* GetHashName() const override;

		private:
			HashFletcher16_state* m_state;
	};
}

#endif // NAZARA_CORE_HASH_FLETCHER16_HPP
