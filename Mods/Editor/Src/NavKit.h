#pragma once
#include <string>
#include <utility>
#include <vector>

#include <Glacier/ZEntity.h>
#include <Glacier/ZMath.h>

struct NavKitMeshTextures {
    // uint32_t m_DiffuseTextureResourceIndex = -1;
    // ZRuntimeResourceID m_DiffuseTextureRuntimeResourceID;
    std::string m_DiffuseTextureHash;

    // uint32_t m_NormalTextureResourceIndex = -1;
    // ZRuntimeResourceID m_NormalTextureRuntimeResourceID;
    std::string m_NormalTextureHash;

    // uint32_t m_SpecularTextureResourceIndex = -1;
    // ZRuntimeResourceID m_SpecularTextureRuntimeResourceID;
    std::string m_SpecularTextureHash;
};

struct NavKitMeshEntity {
    NavKitMeshEntity(
        std::string m_AlocHash,
        std::string m_PrimHash,
        const std::vector<NavKitMeshTextures>& m_MeshTextureInfo,
        const Quat m_Quat,
        std::string m_FolderName,
        std::string m_RoomName,
        const ZEntityRef m_Entity
    ) :
        m_AlocHash(std::move(m_AlocHash)),
        m_PrimHash(std::move(m_PrimHash)),
        m_MeshTextureInfo(m_MeshTextureInfo),
        m_Quat(m_Quat),
        m_RoomName(std::move(m_RoomName)),
        m_FolderName(std::move(m_FolderName)),
        m_Entity(m_Entity) {}

    std::string m_AlocHash;
    std::string m_PrimHash;
    std::vector<NavKitMeshTextures> m_MeshTextureInfo;
    Quat m_Quat;
    std::string m_RoomName;
    std::string m_FolderName;
    ZEntityRef m_Entity;
};
