// Copyright (C) 2015 Jérôme Leclercq
// This file is part of the "Nazara Engine".
// For conditions of distribution and use, see copyright notice in Config.hpp

#pragma once

#ifndef NAZARA_HASH_MD5_HPP
#define NAZARA_HASH_MD5_HPP

#include <Nazara/Prerequesites.hpp>
#include <Nazara/Core/AbstractHash.hpp>
#include <Nazara/Core/HashDigest.hpp>

namespace Nz
{
	struct HashMD5_state;

	class NAZARA_CORE_API HashMD5 : public AbstractHash
	{
		public:
			HashMD5();
			virtual ~HashMD5();

			void Append(const UInt8* data, unsigned int len);
			void Begin();
			HashDigest End();

			static unsigned int GetDigestLength();
			static String GetHashName();

		private:
			HashMD5_state* m_state;
	};
}

#endif // NAZARA_HASH_MD5_HPP
