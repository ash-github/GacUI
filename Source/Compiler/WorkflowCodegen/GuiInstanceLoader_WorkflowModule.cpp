#include "GuiInstanceLoader_WorkflowCodegen.h"
#include "../../Reflection/TypeDescriptors/GuiReflectionEvents.h"
#include "../../Resources/GuiParserManager.h"

namespace vl
{
	namespace presentation
	{
		using namespace workflow;
		using namespace workflow::analyzer;
		using namespace reflection::description;
		using namespace collections;

/***********************************************************************
Workflow_CreateModuleWithUsings
***********************************************************************/

		Ptr<workflow::WfModule> Workflow_CreateModuleWithUsings(Ptr<GuiInstanceContext> context)
		{
			auto module = MakePtr<WfModule>();
			vint index = context->namespaces.Keys().IndexOf(GlobalStringKey());
			if (index != -1)
			{
				auto nss = context->namespaces.Values()[index];
				FOREACH(Ptr<GuiInstanceNamespace>, ns, nss->namespaces)
				{
					auto path = MakePtr<WfModuleUsingPath>();
					module->paths.Add(path);

					auto pathCode = ns->prefix + L"*" + ns->postfix;
					auto reading = pathCode.Buffer();
					while (reading)
					{
						auto delimiter = wcsstr(reading, L"::");
						auto begin = reading;
						auto end = delimiter ? delimiter : begin + wcslen(reading);

						auto wildcard = wcschr(reading, L'*');
						if (wildcard >= end)
						{
							wildcard = nullptr;
						}

						auto item = MakePtr<WfModuleUsingItem>();
						path->items.Add(item);
						if (wildcard)
						{
							if (begin < wildcard)
							{
								auto fragment = MakePtr<WfModuleUsingNameFragment>();
								item->fragments.Add(fragment);
								fragment->name.value = WString(begin, vint(wildcard - begin));
							}
							{
								auto fragment = MakePtr<WfModuleUsingWildCardFragment>();
								item->fragments.Add(fragment);
							}
							if (wildcard + 1 < end)
							{
								auto fragment = MakePtr<WfModuleUsingNameFragment>();
								item->fragments.Add(fragment);
								fragment->name.value = WString(wildcard + 1, vint(end - wildcard - 1));
							}
						}
						else if (begin < end)
						{
							auto fragment = MakePtr<WfModuleUsingNameFragment>();
							item->fragments.Add(fragment);
							fragment->name.value = WString(begin, vint(end - begin));
						}

						if (delimiter)
						{
							reading = delimiter + 2;
						}
						else
						{
							reading = nullptr;
						}
					}
				}
			}
			return module;
		}

/***********************************************************************
Workflow_InstallCtorClass
***********************************************************************/
		
		Ptr<workflow::WfBlockStatement> Workflow_InstallCtorClass(Ptr<GuiInstanceContext> context, types::ResolvingResult& resolvingResult, description::ITypeDescriptor* rootTypeDescriptor, Ptr<workflow::WfModule> module)
		{
			Ptr<WfClassDeclaration> ctorClass;
			{
				auto decls = &module->declarations;
				auto reading = context->className.Buffer();
				while (true)
				{
					auto delimiter = wcsstr(reading, L"::");
					if (delimiter)
					{
						auto ns = MakePtr<WfNamespaceDeclaration>();
						ns->name.value = WString(reading, delimiter - reading);
						decls->Add(ns);
						decls = &ns->declarations;
					}
					else
					{
						ctorClass = MakePtr<WfClassDeclaration>();
						ctorClass->kind = WfClassKind::Class;
						ctorClass->constructorType = WfConstructorType::Undefined;
						ctorClass->name.value = WString(reading) + L"<Ctor>";
						decls->Add(ctorClass);
						break;
					}
					reading = delimiter + 2;
				}
			}

			Workflow_CreateVariablesForReferenceValues(ctorClass, resolvingResult);

			auto thisParam = MakePtr<WfFunctionArgument>();
			thisParam->name.value = L"<this>";
			{
				Ptr<TypeInfoImpl> elementType = new TypeInfoImpl(ITypeInfo::TypeDescriptor);
				elementType->SetTypeDescriptor(rootTypeDescriptor);

				Ptr<TypeInfoImpl> pointerType = new TypeInfoImpl(ITypeInfo::RawPtr);
				pointerType->SetElementType(elementType);

				thisParam->type = GetTypeFromTypeInfo(pointerType.Obj());
			}

			auto resolverParam = MakePtr<WfFunctionArgument>();
			resolverParam->name.value = L"<resolver>";
			{
				Ptr<TypeInfoImpl> elementType = new TypeInfoImpl(ITypeInfo::TypeDescriptor);
				elementType->SetTypeDescriptor(description::GetTypeDescriptor<GuiResourcePathResolver>());

				Ptr<TypeInfoImpl> pointerType = new TypeInfoImpl(ITypeInfo::RawPtr);
				pointerType->SetElementType(elementType);

				resolverParam->type = GetTypeFromTypeInfo(pointerType.Obj());
			}

			auto block = MakePtr<WfBlockStatement>();

			auto func = MakePtr<WfFunctionDeclaration>();
			func->anonymity = WfFunctionAnonymity::Named;
			func->name.value = L"<initialize-instance>";
			func->arguments.Add(thisParam);
			func->arguments.Add(resolverParam);
			func->returnType = GetTypeFromTypeInfo(TypeInfoRetriver<void>::CreateTypeInfo().Obj());
			func->statement = block;

			auto member = MakePtr<WfClassMember>();
			member->kind = WfClassMemberKind::Normal;
			member->declaration = func;
			ctorClass->members.Add(member);

			return block;
		}

/***********************************************************************
Variable
***********************************************************************/

		void Workflow_CreatePointerVariable(Ptr<workflow::WfClassDeclaration> ctorClass, GlobalStringKey name, description::ITypeDescriptor* type, description::ITypeInfo* typeOverride)
		{
			auto var = MakePtr<WfVariableDeclaration>();
			var->name.value = name.ToString();

			if (typeOverride)
			{
				var->type = GetTypeFromTypeInfo(typeOverride);
			}

			if (!var->type)
			{
				if (auto ctors = type->GetConstructorGroup())
				{
					if (ctors->GetMethodCount() > 0)
					{
						auto ctor = ctors->GetMethod(0);
						var->type = GetTypeFromTypeInfo(ctor->GetReturn());
					}
				}
			}

			if (!var->type)
			{
				Ptr<TypeInfoImpl> elementType = new TypeInfoImpl(ITypeInfo::TypeDescriptor);
				elementType->SetTypeDescriptor(type);

				Ptr<TypeInfoImpl> pointerType = new TypeInfoImpl(ITypeInfo::RawPtr);
				pointerType->SetElementType(elementType);

				var->type = GetTypeFromTypeInfo(pointerType.Obj());
			}

			auto literal = MakePtr<WfLiteralExpression>();
			literal->value = WfLiteralValue::Null;
			var->expression = literal;

			auto member = MakePtr<WfClassMember>();
			member->kind = WfClassMemberKind::Normal;
			member->declaration = var;

			ctorClass->members.Add(member);
		}
		
		void Workflow_CreateVariablesForReferenceValues(Ptr<workflow::WfClassDeclaration> ctorClass, types::ResolvingResult& resolvingResult)
		{
			const auto& typeInfos = resolvingResult.typeInfos;
			for (vint i = 0; i < typeInfos.Count(); i++)
			{
				auto key = typeInfos.Keys()[i];
				auto value = typeInfos.Values()[i].typeDescriptor;

				ITypeInfo* typeOverride = nullptr;
				vint index = resolvingResult.typeOverrides.Keys().IndexOf(key);
				if (index != -1)
				{
					typeOverride = resolvingResult.typeOverrides.Values()[index].Obj();
				}
				Workflow_CreatePointerVariable(ctorClass, key, value, typeOverride);
			}
		}
	}
}