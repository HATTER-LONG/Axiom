#include "type_descriptor_copy.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace axiom::introspection::detail {

TypeDescriptor copyTypeDescriptor(const TypeDescriptor& source) {
    std::unordered_map<const TypeDescriptor*, std::shared_ptr<TypeDescriptor>> copies;
    std::vector<const TypeDescriptor*> pending;
    const auto ensure_copy = [&copies, &pending](const TypeDescriptor* node) {
        const auto [position, inserted] = copies.emplace(node, nullptr);
        if(inserted) {
            position->second = std::make_shared<TypeDescriptor>(TypeDescriptor{
                .kind = node->kind,
                .nullable = node->nullable,
                .description = node->description,
                .element_type = nullptr,
                .fields = {},
                .value_type = nullptr,
            });
            pending.push_back(node);
        }
        return position->second;
    };

    ensure_copy(&source);
    while(!pending.empty()) {
        const TypeDescriptor* node = pending.back();
        pending.pop_back();
        TypeDescriptor& copy = *copies.at(node);
        if(node->element_type) {
            copy.element_type = ensure_copy(node->element_type.get());
        }
        if(node->value_type) {
            copy.value_type = ensure_copy(node->value_type.get());
        }
        for(const auto& [name, field] : node->fields) {
            copy.fields.emplace(name, ensure_copy(field.get()));
        }
    }
    return *copies.at(&source);
}

} // namespace axiom::introspection::detail
