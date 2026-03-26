#include "ToastyReplay.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <array>
#include <cmath>
#include <deque>
#include <vector>

using namespace cocos2d;

namespace {
    constexpr ccColor4F kSolidColor       = { 0.00f, 0.25f, 1.00f, 1.00f };
    constexpr ccColor4F kDangerColor      = { 1.00f, 0.00f, 0.00f, 1.00f };
    constexpr ccColor4F kPassableColor    = { 0.00f, 1.00f, 1.00f, 1.00f };
    constexpr ccColor4F kInteractColor    = { 0.00f, 1.00f, 0.00f, 1.00f };
    constexpr ccColor4F kPlayerColor      = { 1.00f, 0.00f, 0.00f, 1.00f };
    constexpr ccColor4F kPlayerInnerColor = { 0.00f, 0.25f, 1.00f, 1.00f };
    constexpr ccColor4F kPlayerAreaColor  = { 0.55f, 0.00f, 0.00f, 1.00f };
    constexpr ccColor4F kCoinColor        = { 0.00f, 1.00f, 0.00f, 1.00f };
    constexpr ccColor4F kSlopeColor       = { 0.00f, 0.25f, 1.00f, 1.00f };
    constexpr float kBorderWidth = 0.25f;
    constexpr unsigned int kCircleSegments = 28;

    ccColor4F transparentFill() {
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    struct ShapeDescriptor {
        enum class Kind {
            Rectangle,
            Triangle,
            OrientedQuad,
            Circle
        };

        Kind kind = Kind::Rectangle;
        std::array<CCPoint, 4> points {};
        size_t pointCount = 0;
        CCRect rect {};
        CCPoint center {};
        float radius = 0.0f;
        ccColor4F fill {};
        ccColor4F border {};
    };

    struct PlayerHitboxSample {
        CCRect outer {};
        CCRect inner {};
        bool subtick = false;
    };

    ShapeDescriptor makeRectangle(CCRect rect, ccColor4F border) {
        ShapeDescriptor descriptor;
        descriptor.kind = ShapeDescriptor::Kind::Rectangle;
        descriptor.rect = rect;
        descriptor.fill = transparentFill();
        descriptor.border = border;
        return descriptor;
    }

    ShapeDescriptor makeCircle(CCPoint center, float radius, ccColor4F border) {
        ShapeDescriptor descriptor;
        descriptor.kind = ShapeDescriptor::Kind::Circle;
        descriptor.center = center;
        descriptor.radius = radius;
        descriptor.fill = transparentFill();
        descriptor.border = border;
        return descriptor;
    }

    ShapeDescriptor makeTriangle(std::array<CCPoint, 3> points, ccColor4F border) {
        ShapeDescriptor descriptor;
        descriptor.kind = ShapeDescriptor::Kind::Triangle;
        descriptor.pointCount = 3;
        for (size_t index = 0; index < 3; ++index) {
            descriptor.points[index] = points[index];
        }
        descriptor.fill = transparentFill();
        descriptor.border = border;
        return descriptor;
    }

    ShapeDescriptor makeOrientedQuad(std::array<CCPoint, 4> points, ccColor4F border) {
        ShapeDescriptor descriptor;
        descriptor.kind = ShapeDescriptor::Kind::OrientedQuad;
        descriptor.pointCount = 4;
        descriptor.points = points;
        descriptor.fill = transparentFill();
        descriptor.border = border;
        return descriptor;
    }

    class OverlayPainter {
    public:
        explicit OverlayPainter(CCDrawNode* drawNode) : m_drawNode(drawNode) {}

        void clear() {
            if (m_drawNode) {
                m_drawNode->clear();
            }
        }

        void paint(std::vector<ShapeDescriptor> const& shapes) {
            if (!m_drawNode) {
                return;
            }

            for (auto const& shape : shapes) {
                switch (shape.kind) {
                    case ShapeDescriptor::Kind::Rectangle: {
                        std::array<CCPoint, 4> corners = {
                            CCPoint(shape.rect.getMinX(), shape.rect.getMinY()),
                            CCPoint(shape.rect.getMinX(), shape.rect.getMaxY()),
                            CCPoint(shape.rect.getMaxX(), shape.rect.getMaxY()),
                            CCPoint(shape.rect.getMaxX(), shape.rect.getMinY())
                        };
                        m_drawNode->drawPolygon(corners.data(), corners.size(), shape.fill, kBorderWidth, shape.border);
                        break;
                    }

                    case ShapeDescriptor::Kind::Triangle:
                    case ShapeDescriptor::Kind::OrientedQuad: {
                        auto polygon = shape.points;
                        m_drawNode->drawPolygon(
                            polygon.data(),
                            static_cast<unsigned int>(shape.pointCount),
                            shape.fill,
                            kBorderWidth,
                            shape.border
                        );
                        break;
                    }

                    case ShapeDescriptor::Kind::Circle:
                        m_drawNode->drawCircle(
                            shape.center,
                            shape.radius,
                            shape.fill,
                            kBorderWidth,
                            shape.border,
                            kCircleSegments
                        );
                        break;
                }
            }
        }

    private:
        CCDrawNode* m_drawNode = nullptr;
    };

    class VisibleObjectSnapshot {
    public:
        static VisibleObjectSnapshot capture(GJBaseGameLayer* layer, bool focusKiller, GameObject* killerObject) {
            VisibleObjectSnapshot snapshot;
            if (!layer) {
                return snapshot;
            }

            if (focusKiller && killerObject) {
                snapshot.m_objects.push_back(killerObject);
                return snapshot;
            }

            int sectionCount = layer->m_sections.empty() ? -1 : static_cast<int>(layer->m_sections.size());
            int startCol = std::min(layer->m_leftSectionIndex, layer->m_rightSectionIndex);
            int endCol = std::max(layer->m_leftSectionIndex, layer->m_rightSectionIndex);
            int startRow = std::min(layer->m_bottomSectionIndex, layer->m_topSectionIndex);
            int endRow = std::max(layer->m_bottomSectionIndex, layer->m_topSectionIndex);

            for (int columnIndex = std::max(0, startCol); columnIndex <= endCol && columnIndex < sectionCount; ++columnIndex) {
                auto* column = layer->m_sections[columnIndex];
                if (!column) {
                    continue;
                }

                int cellCount = static_cast<int>(column->size());
                for (int rowIndex = std::max(0, startRow); rowIndex <= endRow && rowIndex < cellCount; ++rowIndex) {
                    auto* cell = column->at(rowIndex);
                    if (!cell) {
                        continue;
                    }

                    int objectCount = layer->m_sectionSizes[columnIndex]->at(rowIndex);
                    for (int objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
                        if (auto* object = cell->at(objectIndex)) {
                            snapshot.m_objects.push_back(object);
                        }
                    }
                }
            }

            return snapshot;
        }

        std::vector<GameObject*> const& objects() const {
            return m_objects;
        }

    private:
        std::vector<GameObject*> m_objects;
    };

    class TrailHistory {
    public:
        void clear() {
            m_entries[0].clear();
            m_entries[1].clear();
        }

        void capture(GJBaseGameLayer* layer, bool subtick, size_t limit) {
            if (!layer || !layer->m_player1) {
                return;
            }

            capturePlayer(0, layer->m_player1, subtick, limit);
            if (layer->m_gameState.m_isDualMode && layer->m_player2) {
                capturePlayer(1, layer->m_player2, subtick, limit);
            }
        }

        void appendShapes(std::vector<ShapeDescriptor>& out) const {
            appendPlayerShapes(m_entries[0], out);
            appendPlayerShapes(m_entries[1], out);
        }

    private:
        static void trim(std::deque<PlayerHitboxSample>& entries, size_t limit) {
            while (entries.size() > limit) {
                entries.pop_front();
            }
        }

        static void appendPlayerShapes(std::deque<PlayerHitboxSample> const& entries, std::vector<ShapeDescriptor>& out) {
            for (auto const& sample : entries) {
                auto outer = makeRectangle(sample.outer, kPlayerColor);
                auto inner = makeRectangle(sample.inner, kPlayerInnerColor);

                if (sample.subtick) {
                    outer.border.a *= 0.5f;
                    inner.border.a *= 0.5f;
                }

                out.push_back(outer);
                out.push_back(inner);
            }
        }

        void capturePlayer(size_t slot, PlayerObject* player, bool subtick, size_t limit) {
            if (!player) {
                return;
            }

            m_entries[slot].push_back({
                player->getObjectRect(),
                player->getObjectRect(0.25f, 0.25f),
                subtick
            });
            trim(m_entries[slot], limit);
        }

        std::deque<PlayerHitboxSample> m_entries[2];
    };

    class ShapeExtractor {
    public:
        explicit ShapeExtractor(GJBaseGameLayer* layer) : m_layer(layer) {}

        std::vector<ShapeDescriptor> collect(VisibleObjectSnapshot const& snapshot) const {
            std::vector<ShapeDescriptor> shapes;
            shapes.reserve(snapshot.objects().size() + 32);

            for (auto* object : snapshot.objects()) {
                appendObjectShape(object, shapes);
            }

            appendPlayerShape(m_layer ? m_layer->m_player1 : nullptr, shapes);
            if (m_layer && m_layer->m_gameState.m_isDualMode) {
                appendPlayerShape(m_layer->m_player2, shapes);
            }

            return shapes;
        }

    private:
        bool isSpeedModifier(GameObject* object) const {
            if (!object) {
                return false;
            }

            int id = object->m_objectID;
            return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334;
        }

        void appendPlayerShape(PlayerObject* player, std::vector<ShapeDescriptor>& out) const {
            if (!player) {
                return;
            }

            out.push_back(makeRectangle(player->getObjectRect(), kPlayerColor));
            out.push_back(makeRectangle(player->getObjectRect(0.25f, 0.25f), kPlayerInnerColor));

            float radius = player->getObjectRect().size.width * 0.5f;
            if (radius > 0.0f) {
                out.push_back(makeCircle(player->getPosition(), radius, kPlayerAreaColor));
            }
        }

        void appendObjectShape(GameObject* object, std::vector<ShapeDescriptor>& out) const {
            if (!m_layer || !object) {
                return;
            }

            if (object->m_objectType == GameObjectType::Decoration || !object->m_isActivated || object->m_isGroupDisabled) {
                return;
            }

            if (object == static_cast<GameObject*>(m_layer->m_player1) || object == static_cast<GameObject*>(m_layer->m_player2)) {
                return;
            }

            switch (object->m_objectType) {
                case GameObjectType::Solid: {
                    out.push_back(makeRectangle(object->getObjectRect(), object->m_isPassable ? kPassableColor : kSolidColor));
                    return;
                }

                case GameObjectType::Slope: {
                    auto rect = object->getObjectRect();
                    std::array<CCPoint, 3> triangle = {
                        CCPoint(rect.getMinX(), rect.getMinY()),
                        CCPoint(rect.getMinX(), rect.getMaxY()),
                        CCPoint(rect.getMaxX(), rect.getMinY())
                    };
                    CCPoint topRight(rect.getMaxX(), rect.getMaxY());
                    switch (object->m_slopeDirection) {
                        case 0:
                        case 7:
                            triangle[1] = topRight;
                            break;
                        case 1:
                        case 5:
                            triangle[0] = topRight;
                            break;
                        case 3:
                        case 6:
                            triangle[2] = topRight;
                            break;
                        default:
                            break;
                    }
                    out.push_back(makeTriangle(triangle, object->m_isPassable ? kPassableColor : kSlopeColor));
                    return;
                }

                case GameObjectType::Hazard:
                case GameObjectType::AnimatedHazard: {
                    if (object == m_layer->m_anticheatSpike) {
                        return;
                    }

                    float radius = object->getObjectRadius();
                    if (radius > 0.0f) {
                        out.push_back(makeCircle(object->getPosition(), radius, kDangerColor));
                        return;
                    }

                    if (auto* orientedBox = m_layer->m_isEditor ? object->getOrientedBox() : object->m_orientedBox) {
                        out.push_back(makeOrientedQuad(orientedBox->m_corners, kDangerColor));
                        return;
                    }

                    auto dirtyRect = object->m_isObjectRectDirty;
                    auto boxOffsetCalculated = object->m_boxOffsetCalculated;
                    out.push_back(makeRectangle(object->getObjectRect(), kDangerColor));
                    object->m_isObjectRectDirty = dirtyRect;
                    object->m_boxOffsetCalculated = boxOffsetCalculated;
                    return;
                }

                case GameObjectType::SecretCoin:
                case GameObjectType::UserCoin:
                case GameObjectType::Collectible:
                    out.push_back(makeRectangle(object->getObjectRect(), kCoinColor));
                    return;

                case GameObjectType::CollisionObject:
                    return;

                case GameObjectType::Modifier:
                    if (!isSpeedModifier(object)) {
                        return;
                    }
                    [[fallthrough]];

                default: {
                    if (auto* orientedBox = m_layer->m_isEditor ? object->getOrientedBox() : object->m_orientedBox) {
                        out.push_back(makeOrientedQuad(orientedBox->m_corners, kInteractColor));
                    } else {
                        out.push_back(makeRectangle(object->getObjectRect(), kInteractColor));
                    }
                    return;
                }
            }
        }

        GJBaseGameLayer* m_layer = nullptr;
    };

    class HitboxOverlayState {
    public:
        static HitboxOverlayState& get() {
            static HitboxOverlayState instance;
            return instance;
        }

        void attach(GJBaseGameLayer* layer) {
            m_overlayNode = nullptr;
            resetAttempt();

            if (!layer || !layer->m_debugDrawNode) {
                return;
            }

            auto* parent = layer->m_debugDrawNode->getParent();
            if (!parent) {
                return;
            }

            auto* node = CCDrawNode::create();
            node->setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
            node->m_bUseArea = false;
            parent->addChild(node, 1402);
            m_overlayNode = node;
        }

        void detach() {
            m_overlayNode = nullptr;
            resetAttempt();
        }

        void resetAttempt() {
            m_dead = false;
            m_killerObject = nullptr;
            m_trails.clear();
        }

        void markCollision(GameObject* object, GameObject* anticheatSpike) {
            if (object && object != anticheatSpike) {
                m_killerObject = object;
            }
        }

        void markDead(bool dead) {
            m_dead = dead;
        }

        void captureTrail(GJBaseGameLayer* layer, bool subtick) {
            auto* engine = ReplayEngine::get();
            if (!engine || m_dead || !engine->showHitboxes || !engine->hitboxTrail) {
                return;
            }

            size_t capacity = static_cast<size_t>(engine->hitboxTrailLength);
            m_trails.capture(layer, subtick, capacity);
        }

        void render(GJBaseGameLayer* layer) {
            OverlayPainter painter(m_overlayNode);
            painter.clear();

            auto* engine = ReplayEngine::get();
            if (!engine || !layer || !m_overlayNode) {
                return;
            }

            if (!engine->showHitboxes || (engine->hitboxOnDeath && !m_dead)) {
                return;
            }

            auto snapshot = VisibleObjectSnapshot::capture(layer, engine->hitboxOnDeath && m_dead, m_killerObject);
            ShapeExtractor extractor(layer);
            auto shapes = extractor.collect(snapshot);

            if (engine->hitboxTrail) {
                m_trails.appendShapes(shapes);
            }

            painter.paint(shapes);
        }

    private:
        CCDrawNode* m_overlayNode = nullptr;
        TrailHistory m_trails;
        bool m_dead = false;
        GameObject* m_killerObject = nullptr;
    };
}

class $modify(HitboxBGL, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (!PlayLayer::get()) return;
        HitboxOverlayState::get().captureTrail(this, isHalfTick);
    }
};

class $modify(HitboxPL, PlayLayer) {
    void createObjectsFromSetupFinished() {
        PlayLayer::createObjectsFromSetupFinished();
        HitboxOverlayState::get().attach(this);
    }

    void updateVisibility(float dt) {
        PlayLayer::updateVisibility(dt);
        HitboxOverlayState::get().markDead(m_player1 && m_player1->m_isDead);
        HitboxOverlayState::get().render(this);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        HitboxOverlayState::get().resetAttempt();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        HitboxOverlayState::get().markCollision(object, m_anticheatSpike);
    }

    void onQuit() {
        HitboxOverlayState::get().detach();
        PlayLayer::onQuit();
    }
};

class $modify(HitboxPO, PlayerObject) {
    void playerDestroyed(bool value) {
        if (auto* playLayer = PlayLayer::get()) {
            if (this == playLayer->m_player1 || this == playLayer->m_player2) {
                HitboxOverlayState::get().markDead(true);
            }
        }
        PlayerObject::playerDestroyed(value);
    }
};
